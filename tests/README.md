# Host tests

The tests compile the production C driver and renderer against a fake HAL. No serial port or panel is accessed. They check packet data, 296-gate scan order, the temperature schedules, graphics, fault cleanup, image packing, the finite showcase plan, and the PC protocol client. Host rendering produces image previews with synthetic temperature and timing values; it does not measure optical quality.

The bundled waveform, generated header, and binary fixture are present in a fresh clone. Run from the repository root:

```sh
python tests/run-host-tests.py
```

To recheck and regenerate the derived files, run `python tools/import_vendor_waveform.py`. The importer verifies a 74472-byte table against SHA256 `48b099be4e104e55052baafb70fac10f2c2fa225e83bd2f67ec18452fa5b7a86`; the test runner independently checks the header and fixture. Results are written under the ignored `build-host/` directory. The waveform is third-party material outside this project's MIT license; see [SOURCES.md](../SOURCES.md).

Set `CC` or use `--cc /path/to/gcc` to select a C11 compiler. `--sanitize` requests AddressSanitizer and UndefinedBehaviorSanitizer where supported. Python tool tests are included in the runner and can also be run separately:

```sh
python tests/test_hosttool.py
python tests/test_serial_client.py
```

The showcase checks its 12 scheduled updates and waveform frame budgets at every integer temperature from 0 to 40 °C. A2 test frames must be binary and all changes must stay in the declared content rectangle; the panel still scans 296 rows. Software checks do not establish display life, DC balance, analog voltages, or an absence of visible ghosting.
