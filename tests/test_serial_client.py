"""Exercise the real bounded Client with a fake serial transport, no hardware.

Packets may split at any byte, including CRLF and completion tokens. A virtual
monotonic clock makes missing acknowledgements deterministic and instantaneous.
"""
import binascii
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("et014_serial_under_test", ROOT / "tools/epdtool.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class Clock:
    def __init__(self): self.now = 0.0
    def monotonic(self): self.now += 0.00001; return self.now


class FakeSerial:
    def __init__(self, clock, ready, acknowledgement, refresh=None):
        self.clock = clock
        self.ready = list(ready)
        self.acknowledgement = list(acknowledgement)
        self.refresh = list(refresh or [b"DONE refresh gc4 result=0\r\n"])
        self.queue = []
        self.writes = []
        self.is_open = False
        self.was_closed = False
        self.read_chunks = []

    @property
    def in_waiting(self): return len(self.queue[0]) if self.queue else 0
    def open(self):
        assert self.dtr is False and self.rts is False
        self.is_open = True
    def close(self): self.is_open = False; self.was_closed = True
    def flush(self): pass
    def write(self, data):
        assert self.is_open
        data = bytes(data); self.writes.append(data)
        if data == b"help\n":
            self.queue.extend([b"ET014 ", b"DRIVER 1.0\r\nAvailable commands\r\n"])
        elif data.startswith(b"load "):
            self.queue.extend(self.ready)
        elif len(data) == tool.BYTES:
            self.queue.extend(self.acknowledgement)
        elif data == b"refresh gc4\n":
            self.queue.extend(self.refresh)
        else:
            raise AssertionError("Unexpected write: " + repr(data[:80]))
        return len(data)
    def read(self, length):
        assert self.is_open
        self.clock.now += .01 if self.queue else .1
        if not self.queue: return b""
        block = self.queue[0][:length]
        self.queue[0] = self.queue[0][length:]
        if not self.queue[0]: self.queue.pop(0)
        self.read_chunks.append(block)
        return block


class SerialClientTest(unittest.TestCase):
    data = bytes(range(256)) * (tool.BYTES // 256)
    data += bytes(range(tool.BYTES - len(data)))
    crc = binascii.crc32(data) & 0xffffffff

    def fixture(self, acknowledgement, ready=None, refresh=None):
        clock = Clock()
        serial = FakeSerial(clock, ready or [b"READY 9472\r\n"], acknowledgement, refresh)
        module = types.SimpleNamespace(Serial=lambda: serial)
        return clock, serial, module

    def run_upload(self, acknowledgement, ready=None):
        clock, serial, module = self.fixture(acknowledgement, ready)
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with tool.Client("FAKE-NO-HARDWARE") as client:
                client.upload(self.data)
                self.assertIn(b"DONE load result=0\r\n", client.transcript)
                self.assertFalse(serial.queue, "Upload returned before consuming its completion")
                self.assertEqual(serial.writes[-2], f"load {self.crc:08X}\n".encode())
                self.assertEqual(serial.writes[-1], self.data)
        self.assertTrue(serial.was_closed)

    def test_combined_acknowledgement(self):
        self.run_upload([f"LOAD OK {self.crc:08X}\r\nDONE load result=0\r\n".encode()])

    def test_split_every_protocol_boundary(self):
        ready = [b"RE", b"ADY ", b"947", b"2\r", b"\n"]
        ack = [b"LOAD ", b"OK ", f"{self.crc:08X}".encode(), b"\r", b"\nDO", b"NE load ", b"result=", b"0\r", b"\n"]
        self.run_upload(ack, ready)

    def test_each_byte_separate(self):
        # read_until must preserve tokens that straddle arbitrary transport reads.
        ack = f"LOAD OK {self.crc:08X}\r\nDONE load result=0\r\n".encode()
        self.run_upload([bytes([b]) for b in ack], [bytes([b]) for b in b"READY 9472\r\n"])

    def test_load_ok_without_done_is_not_success(self):
        clock, serial, module = self.fixture([f"LOAD OK {self.crc:08X}\r\n".encode()])
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with self.assertRaises(TimeoutError):
                with tool.Client("FAKE-NO-HARDWARE") as client: client.upload(self.data)
        self.assertTrue(serial.was_closed)

    def test_done_without_matching_crc_rejected(self):
        for response in (b"DONE load result=0\r\n", b"LOAD OK DEADBEEF\r\nDONE load result=0\r\n"):
            with self.subTest(response=response):
                clock, serial, module = self.fixture([response])
                with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
                    with self.assertRaises(RuntimeError):
                        with tool.Client("FAKE-NO-HARDWARE") as client: client.upload(self.data)
                self.assertTrue(serial.was_closed)

    def test_error_response_rejected(self):
        clock, serial, module = self.fixture([b"ERR CRC mismatch\r\n", b"DONE load result=-3\r\n"])
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with self.assertRaises(RuntimeError):
                with tool.Client("FAKE-NO-HARDWARE") as client: client.upload(self.data)
        self.assertTrue(serial.was_closed)

    def test_next_refresh_waits_for_its_own_done(self):
        ack = [f"LOAD OK {self.crc:08X}\r\n".encode(), b"DONE load ", b"result=0\r\n"]
        # An intentionally stale duplicate load DONE cannot complete refresh.
        response = [b"DONE load result=0\r\n", b"PROFILE passed\r\n", b"DONE refresh gc4 result=0\r\n"]
        clock, serial, module = self.fixture(ack, refresh=response)
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with tool.Client("FAKE-NO-HARDWARE") as client:
                client.upload(self.data)
                self.assertFalse(serial.queue)
                result = client.command("refresh gc4")
                self.assertIn(b"DONE refresh gc4", result)
                self.assertFalse(serial.queue)
        self.assertTrue(serial.was_closed)

    def test_failed_upload_does_not_send_refresh(self):
        clock, serial, module = self.fixture([b"ERR CRC\r\n"])
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with self.assertRaises(RuntimeError):
                with tool.Client("FAKE-NO-HARDWARE") as client:
                    client.upload(self.data)
                    client.command("refresh gc4")
        self.assertNotIn(b"refresh gc4\n", serial.writes)

    def test_command_waits_for_complete_result_line(self):
        chunks = [b"DONE refresh gc4", b" result=", b"0\r", b"\n"]
        clock, serial, module = self.fixture([], refresh=chunks)
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with tool.Client("FAKE-NO-HARDWARE") as client:
                result = client.command("refresh gc4")
                self.assertIn(b"DONE refresh gc4 result=0\r\n", result)
                self.assertFalse(serial.queue, "Command returned at completion prefix instead of complete line")

    def test_command_rejects_nonzero_result(self):
        clock, serial, module = self.fixture([], refresh=[b"DONE refresh gc4", b" result=-3\r\n"])
        with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
            with self.assertRaises(RuntimeError):
                with tool.Client("FAKE-NO-HARDWARE") as client: client.command("refresh gc4")
        self.assertTrue(serial.was_closed)

    def test_log_written_and_port_closed_on_error(self):
        clock, serial, module = self.fixture([b"ERR CRC mismatch\r\n"])
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "nested/session.log"
            with patch.dict(sys.modules, {"serial": module}), patch.object(tool, "time", clock):
                with self.assertRaises(RuntimeError):
                    with tool.Client("FAKE-NO-HARDWARE", log) as client: client.upload(self.data)
            self.assertIn(b"ERR CRC mismatch", log.read_bytes())
        self.assertTrue(serial.was_closed)


if __name__ == "__main__": unittest.main()
