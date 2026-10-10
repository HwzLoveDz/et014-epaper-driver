# 来源与资料范围 / Sources

## 实现依据

本驱动针对 ET014TT1 / GDEP014TT1 的 software-TCON 接口整理，参考资料为原厂 `GDEP014TT1 code`、`GDEP014TT1 V0.4 Specification.pdf`、`ET014TT1(A33) Ver 0.4.pdf` 和 ET014TT1 示例。

温区波形来自原厂示例的 `Sample/EVK-ST401VE_SWTCON/Src/WaveformData.c`；参考电源与扫描流程来自同目录 `main.c`。接口包格式通过原厂代码、SWTCON Cortex-M4 库分析和逐字节测试核对。ESP32-S3 端口、绘图库、PC 工具和有限演示在此基础上独立分层。

原厂波形包含 10 个温点、INIT / GC4 / GU4 / A2 四种模式，展开后共 **74472 字节**，预期 SHA256：

```text
48b099be4e104e55052baafb70fac10f2c2fa225e83bd2f67ec18452fa5b7a86
```

波形保持原始数据，不重建 LUT，也不混入其他型号参数。

## 本地导入

原厂波形头文件、测试用原厂二进制 fixture、原始资料和预编译固件不包含在公开仓库内。开发者需自行取得有权使用的匹配资料，在仓库根目录执行：

```sh
python tools/import_vendor_waveform.py "path/to/WaveformData.c"
```

导入数据供本地构建与测试使用。构建和波形测试需要这份数据；仓库不自动下载，也不赋予原厂资料新的使用或再分发权利。

## 许可

项目代码许可证尚待确定。原厂资料和数据保留原始归属；当前没有可确认的独立开源授权，因此未将其纳入公开仓库，也未为其指定项目许可证。

设备 Flash 备份、个人设备标识、私人工作目录和原厂压缩包不属于本仓库发布资料。

## English

The driver targets the ET014TT1 / GDEP014TT1 software-TCON interface. Its implementation was checked against vendor examples and specifications, the Cortex-M4 SWTCON library, and byte-level tests. The ESP32-S3 port, graphics library, PC tools, and finite showcase are separated into project components.

Vendor `WaveformData.c` contains ten temperature points and four modes: INIT, GC4, GU4, and A2. The expanded data is 74472 bytes and must match the SHA256 above. The project does not reconstruct the LUT or substitute another panel's data.

Vendor waveform headers, the vendor binary test fixture, original documents, and prebuilt firmware are excluded. Obtain authorized matching materials and run the local import command above before building or running waveform-dependent tests. Importing does not grant additional rights to use or redistribute those materials.

The project code license is yet to be finalized. Vendor data retains its original ownership and is not covered by a project license. Device backups and private identifiers are also excluded.
