# 来源与资料范围 / Sources

## 实现依据

本驱动针对 ET014TT1 / GDEP014TT1 的 software-TCON 接口整理，参考资料为原厂 `GDEP014TT1 code`、`GDEP014TT1 V0.4 Specification.pdf`、`ET014TT1(A33) Ver 0.4.pdf` 和 ET014TT1 示例。

温区波形来自随屏幕提供的 E Ink SW-TCON 示例 `Sample/EVK-ST401VE_SWTCON/Src/WaveformData.c`，现按原样收录于 `third_party/eink/GDEP014TT1/WaveformData.c`；参考电源与扫描流程来自同目录 `main.c`。接口包格式通过原厂代码、SWTCON Cortex-M4 库分析和逐字节测试核对。ESP32-S3 端口、绘图库、PC 工具和有限演示在此基础上独立分层。

原厂波形包含 10 个温点、INIT / GC4 / GU4 / A2 四种模式，展开后共 **74472 字节**，预期 SHA256：

```text
48b099be4e104e55052baafb70fac10f2c2fa225e83bd2f67ec18452fa5b7a86
```

原始 C 文件 SHA256：`65207e33833b14652bfbd25e91fd2121cf35b36f63df29a2a23e99d90dc9e78f`。波形保持原始数据，不重建 LUT，也不混入其他型号参数。

## 仓库内文件与再生成

公开仓库包含原始 `WaveformData.c`、驱动使用的 `components/et014/src/et014_waveform_data.h` 和用于逐字节校验的 `tests/fixtures/vendor-waveform.bin`。干净 clone 后无需额外导入即可编译与运行测试。要验证并重新生成后两份文件，在仓库根目录执行：

```sh
python tools/import_vendor_waveform.py
```

导入脚本同时支持传入另一个 `WaveformData.c` 路径，但只接受与上述 SHA256 完全一致的数据。设备 Flash 备份、原厂完整资料包和预编译固件不在仓库内。

## 许可

本仓库中由本项目创作并发布的文件采用 [MIT 许可证](LICENSE)。E Ink 波形原文件没有附独立开源许可证，随包 `main.c` 标有 E Ink 的版权和 “All rights reserved”；我们尚未核实该波形的公开再分发权。**仓库 MIT 许可证不适用于 `third_party/eink/GDEP014TT1/WaveformData.c`、生成的波形头文件或二进制 fixture，也不表示第三方资料已获开源授权。**

个人设备标识、私人工作目录和原厂压缩包不属于本仓库发布资料。若你需要在产品中使用或继续分发这份波形，请向原权利方确认适用条款。

## English

The driver targets the ET014TT1 / GDEP014TT1 software-TCON interface. Its implementation was checked against vendor examples and specifications, the Cortex-M4 SWTCON library, and byte-level tests. The ESP32-S3 port, graphics library, PC tools, and finite showcase are separated into project components.

Vendor `WaveformData.c` contains ten temperature points and four modes: INIT, GC4, GU4, and A2. The expanded data is 74472 bytes and must match the SHA256 above. The project does not reconstruct the LUT or substitute another panel's data.

The public repository includes the original third-party `WaveformData.c`, its generated driver header, and a binary test fixture. A fresh clone can build and run tests without an import step; `python tools/import_vendor_waveform.py` verifies and regenerates the two derived files. Device backups, the complete vendor archive, and prebuilt firmware remain excluded.

Project-authored files are released under the [MIT License](LICENSE). The original E Ink waveform carries no separate open-source license in the supplied file, and its public redistribution rights have not been verified. The repository MIT license does **not** cover the original waveform, the generated header, or the binary fixture. Users should confirm applicable terms with the rightsholder before further use or redistribution.
