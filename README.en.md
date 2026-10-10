# ET014 E-paper Driver

[中文](README.md) | **English**

A C driver for **ET014TT1 / GDEP014TT1 128 × 296, four-level grayscale e-paper**, with a portable core, graphics library, ESP32-S3 port, serial console, and PC image tools.

I separated panel driving, drawing, and platform interfaces so the library can be used in ESP-IDF projects or ported to another MCU. Full-page updates use GC4 by default. A bounded A2 ball animation demonstrates the path from host rendering to a physical display using the same drawing code.

![Landscape host preview rendered by the production C graphics code; temperature and timing use test data](reports/showcase/overview.png)

This is a host preview, not a device photograph. See [Showcase](docs/SHOWCASE.md) for the sequence.

## Features

- Native 128 × 296, 2bpp framebuffer with black, dark gray, light gray, and white; supports a 296 × 128 landscape layout.
- Automatic INIT + GC4 when display history is unknown; GC4 for normal updates and no refresh when pixels are unchanged.
- Lines, circles, rectangles, fills, clipping, inversion, progress bars, four orientations, and a scalable ASCII bitmap font.
- 1bpp / 2bpp images and threshold, Bayer, or Floyd–Steinberg grayscale conversion.
- A dedicated FreeRTOS display task, cancellation between scan frames, error cleanup, and refresh statistics.
- CRC32-checked serial image transfer and a finite 12-step demo.

UTF-8 parsing and glyph callbacks allow an external Chinese font, but Chinese glyphs are not bundled. Rectangle updates limit changed content while still scanning all 296 rows.

## Requirements

The ESP32-S3 port targets **ESP-IDF 5.5, 240 MHz, and exclusive SPI2 ownership**. It uses private SPI LL interfaces. Other SDK versions and MCUs require an adapted and validated HAL.

Vendor temperature-dependent waveforms are not distributed in this repository. Obtain `WaveformData.c` from ET014TT1 / GDEP014TT1 vendor materials you are authorized to use, then import it from the repository root:

```sh
python tools/import_vendor_waveform.py "path/to/WaveformData.c"
```

Import the data before building or running waveform-dependent tests. See [Sources](SOURCES.md). Do not substitute a LUT from another panel model.

### Default wiring

Supply the panel interface with 3.3 V and connect a common ground.

| Panel signal | ESP32-S3 GPIO |
| --- | --- |
| PWRON | 4 |
| RESETB | 5 |
| BUSY | 6 |
| OEI | 7 |
| CS | 10 |
| MOSI | 11 |
| SCK | 12 |
| MISO | 13 |

UART0 uses GPIO43 / 44; the native USB Serial/JTAG console is also available. Pins can be changed through the port configuration. The example needs exclusive use of these pins and SPI2.

The default reference VCOM is **−2960 mV**, taken from the vendor main example. The tested sample has no calibration label, and analog drive voltage and power consumption have not been measured. Check the documentation for your sample before using this value.

## Build and first run

Open a configured ESP-IDF 5.5 shell and import the vendor waveforms first:

```sh
cd examples/esp32s3
idf.py set-target esp32s3
idf.py build
```

Flashing writes the connected device's Flash. Check the port, board, partition table, and existing data first. Standard `idf.py flash` writes the generated bootloader, partition table, and application; for an application-only update, choose the image and offset from your actual partition layout.

```sh
idf.py -p <PORT> flash monitor
```

The console uses 115200 and accepts LF or CRLF-terminated commands. A normal cold start waits 10 seconds, runs the demo once, retains the final image, and shuts down panel drive power.

```text
profile
probe
temp
city
refresh clean
```

`profile` checks scan speed with PWR low and CS high. `probe` should report ID `0x12`. `city` only changes RAM; `refresh clean` performs INIT + GC4. To bring up a new port step by step, send `showcase stop` during the boot delay to cancel the automatic demo, check speed and identity, then refresh.

## Console and PC image transfer

| Command | Purpose |
| --- | --- |
| `showcase run` / `status` / `stop` | Replay, inspect, or stop the finite demo |
| `demo` / `fill 3` / `invert` | Draw, clear to white, or invert RAM pixels |
| `refresh auto` | INIT + GC4 for unknown history; GC4 for known history |
| `refresh gc4 0 0 64 64` | Update content in a native-coordinate rectangle |
| `status` / `cancel` | Inspect state or request cancellation between frames |

`rotate 90` changes coordinates for subsequent drawing without rotating existing pixels. Do not modify the framebuffer concurrently with a refresh.

Run the following PC commands from the repository root. Image packing uses the Python standard library. Install `pyserial` for serial access and optionally `Pillow` for PNG / JPEG input.

```sh
python -m pip install pyserial Pillow
python tools/epdtool.py ports
python tools/epdtool.py command --port <PORT> "profile"
python tools/epdtool.py upload --port <PORT> assets/pixel-city-2bpp.bin --refresh clean
python tools/epdtool.py command --port <PORT> --timeout 150 "showcase run"
```

The tool does not select a port or reset the device automatically. Transfers use a fixed 9472-byte 2bpp frame and CRC32; failed CRC or a transfer timeout leaves the canvas unchanged. Uploading and refreshing are separate operations; `--refresh` explicitly requests the subsequent refresh.

## Refresh modes and device results

GC4 is the default full-page mode. Repeated GU4 page changes produced reduced clarity and slight ghosting on the tested panel, so the automatic demo now uses GC4 for every page. GU4 / A2 require explicit experimental-mode enablement. A2 requires both old and new images to be binary black and white.

| Check | Verified scope |
| --- | --- |
| GC4 grayscale | Black, dark gray, light gray, and white are distinguishable on the sample; text and graphics are clear |
| INIT + GC4 | 121 + 31 frames at 32 °C; about 3.55 seconds for the complete operation |
| Finite A2 animation | The ball is created and moved with A2; the 32 °C comparison was visually confirmed to clear the initial position |
| Complete v1.1.3 demo | 12 updates, 534 scan frames, 75.208 seconds at 29 °C; GC4 × 7, A2 × 5, INIT × 2 |

These results apply to the tested sample and reference VCOM. The complete v1.1.3 run finished successfully; final visual acceptance of that run's image quality and pacing is still pending. Host tests cover waveform budgets from 0–40 °C. Full-range optical performance, endurance, power, and analog drive voltages have not been measured on hardware.

The demo runs once, with 58.5 seconds of planned dwell time plus refresh and power-sequencing overhead. Manual replay has a 60-second cooldown. The ball uses at most five A2 updates, and the display stops refreshing at the end. These values limit example activity; they are not panel endurance ratings.

## Porting, tests, and layout

Implement `et014_hal_t` for another platform. The core is synchronous; the example provides asynchronous command handling through one display-owner task. Old/new frames and scan buffers use about 29 KB RAM. The 74472-byte waveform data resides in Flash. Validate row timing and scan speed when bringing up a new HAL.

```text
components/et014/          C core, graphics, temperature waveform interface
components/et014_esp32/    ESP32-S3 port
components/et014_showcase/ Shared renderer and 12-step plan
examples/esp32s3/         Console and display task
tools/epdtool.py          Image processing, transfer, and commands
tests/                    Fake HAL, waveform, graphics, and PC tool tests
docs/SHOWCASE.md          Demo sequence and previews
```

After importing vendor data, run these commands with Python and a C compiler available:

```sh
python tests/run-host-tests.py
python tests/test_hosttool.py
```

Host tests check emitted driver data, cleanup, drawing, and the demo plan. They do not access a device or measure ghosting, drive voltage, or endurance. A restart does not infer the image left on the panel. Errors and cancellation invalidate display history so the next refresh initializes it again.

## Sources and licensing

This repository publishes project source, examples, and tests. The code license is yet to be finalized. Vendor waveform data, original vendor documents, and device backups are not included; their rights remain with their respective owners. See [SOURCES.md](SOURCES.md).

See [CHANGELOG.md](CHANGELOG.md) for version history. When reporting a problem, include the exact panel model, SDK version, refresh mode, temperature, and reproduction steps.
