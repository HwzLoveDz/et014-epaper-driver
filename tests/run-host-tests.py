#!/usr/bin/env python3
"""Compile and execute production driver with deterministic fake HAL. No device I/O.

Use --cc /path/to/gcc or CC to override autodetection. Artifacts are deliberately
retained under build-host/. Every child process is synchronous and bounded.
"""
from __future__ import annotations
import argparse
import binascii
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build-host"
EXPECTED_SHA256 = "48b099be4e104e55052baafb70fac10f2c2fa225e83bd2f67ec18452fa5b7a86"


def preview_pgm(pgm):
    """Lossless P5 grayscale preview for native OR logical showcase orientation."""
    data = pgm.read_bytes()
    header = re.match(rb"P5\s+(\d+)\s+(\d+)\s+255\r?\n", data)
    if not header:
        raise RuntimeError(f"Unsupported renderer PGM header: {pgm}")
    width, height = map(int, header.groups())
    pixels = data[header.end():]
    if width <= 0 or height <= 0 or len(pixels) != width * height:
        raise RuntimeError(f"Renderer PGM length mismatch: {pgm}")
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", binascii.crc32(tag + body) & 0xffffffff)
    rows = b"".join(b"\0" + pixels[y*width:(y+1)*width] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    output = pgm.with_suffix(".png")
    output.write_bytes(png)
    return {"width": width, "height": height, "png_sha256": hashlib.sha256(png).hexdigest()}


def run(cmd, env, timeout=90):
    result = subprocess.run([str(x) for x in cmd], cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=timeout, errors="replace")
    print(result.stdout, end="", flush=True)
    return {"command": [str(x) for x in cmd], "returncode": result.returncode,
            "output": result.stdout}


def get_waveform():
    """Expected bytes live in a digest-locked fixture independent of production."""
    fixture = ROOT / "tests/fixtures/vendor-waveform.bin"
    if not fixture.is_file():
        raise RuntimeError("Missing local supplier waveform; run: python tools/import_vendor_waveform.py <WaveformData.c>")
    data = fixture.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(f"Independent waveform fixture digest mismatch: {digest}")
    production = ROOT / "components/et014/src/et014_waveform_data.h"
    if not production.is_file():
        raise RuntimeError("Missing local waveform header; run: python tools/import_vendor_waveform.py <WaveformData.c>")
    production_data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})\b", production.read_text()))
    if production_data != data:
        raise RuntimeError("Production waveform differs from exact supplied binary")
    out = BUILD / "reference-waveform.bin"
    out.write_bytes(data)
    return out, {"sha256": digest, "bytes": len(data), "source": str(fixture),
                 "production_byte_match": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC"))
    parser.add_argument("--sanitize", action="store_true", help="Require ASan/UBSan build where toolchain supports it")
    args = parser.parse_args()
    candidates = [args.cc, shutil.which("gcc"), shutil.which("clang")]
    compiler = next((Path(p).resolve() for p in candidates if p and Path(p).is_file()), None)
    if compiler is None:
        raise RuntimeError("No host C compiler found; use --cc")
    BUILD.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    report = {"kind": "host fake HAL; not physical validation", "started_unix": time.time(),
              "compiler": str(compiler), "sanitize": args.sanitize, "runs": [], "passed": False}
    try:
        report["compiler_version"] = run([compiler, "--version"], env)["output"].splitlines()[0]
        blob, report["waveform"] = get_waveform()
        component = ROOT / "components/et014"
        sources = sorted((component / "src").glob("*.c"))
        if not sources:
            raise RuntimeError("Core sources have not been written yet")
        flags = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-I", component / "include"]
        showcase = ROOT / "components/et014_showcase"
        if showcase.exists():
            sources += sorted(showcase.glob("*.c"))
            sources += sorted((showcase / "src").glob("*.c"))
            flags += ["-I", showcase / "include"]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        suites = [(ROOT / "tests/test_driver.c", [str(blob)])]
        gfx = ROOT / "tests/test_gfx.c"
        if gfx.exists():
            suites.append((gfx, []))
        showcase_test = ROOT / "tests/test_showcase.c"
        if showcase_test.exists():
            suites.append((showcase_test, [str(blob)]))
        renderer = ROOT / "tests/render_gfx.c"
        render_out = BUILD / "gfx-renders"
        if renderer.exists():
            render_out.mkdir(parents=True, exist_ok=True)
            suites.append((renderer, [str(render_out)]))
        showcase_renderer = ROOT / "tests/render_showcase.c"
        showcase_out = ROOT / "reports/showcase"
        if showcase_renderer.exists():
            showcase_out.mkdir(parents=True, exist_ok=True)
            suites.append((showcase_renderer, [str(showcase_out)]))
        for test, arguments in suites:
            output = BUILD / (test.stem + (".exe" if os.name == "nt" else ""))
            result = run([compiler, *flags, *sources, test, "-o", output], env)
            report["runs"].append(result)
            if result["returncode"]:
                raise RuntimeError(f"Compilation failed: {test.name}")
            result = run([output, *arguments], env, timeout=120)
            report["runs"].append(result)
            if result["returncode"]:
                raise RuntimeError(f"Test suite failed: {test.name}")
            report.setdefault("binaries", {})[output.name] = hashlib.sha256(output.read_bytes()).hexdigest()
        if renderer.exists():
            report["rendered"] = {}
            for pgm in sorted(render_out.glob("gfx-rotation-*.pgm")):
                result = run([sys.executable, ROOT / "tools/epdtool.py", "pack", pgm,
                              pgm.with_suffix(".bin"), "--preview", pgm.with_suffix(".png")], env)
                report["runs"].append(result)
                if result["returncode"]:
                    raise RuntimeError("Host graphics preview conversion failed")
                report["rendered"][pgm.name] = hashlib.sha256(pgm.read_bytes()).hexdigest()
        if showcase_renderer.exists():
            report["showcase_previews"] = {}
            for pgm in sorted(showcase_out.glob("*.pgm")):
                report["showcase_previews"][pgm.name] = preview_pgm(pgm)
        for test_name in ("test_hosttool.py", "test_serial_client.py"):
            host_tool = ROOT / "tests" / test_name
            if host_tool.exists():
                result = run([sys.executable, host_tool], env)
                report["runs"].append(result)
                if result["returncode"]:
                    raise RuntimeError(f"Host image/protocol tests failed: {test_name}")
        report["passed"] = True
        print("All host suites passed. No COM port, GPIO or physical panel used.")
    finally:
        report["finished_unix"] = time.time()
        (BUILD / "host-test-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if (ROOT / "components/et014_showcase").exists():
            output = ROOT / "reports/showcase"
            output.mkdir(parents=True, exist_ok=True)
            (output / "host-test-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
