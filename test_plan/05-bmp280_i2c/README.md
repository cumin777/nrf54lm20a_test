# BMP280 I2C Demo for XIAO nRF54LM20A

这个工程用于驱动 `优信电子 GY-BMP280` 模块，走 I2C 接口，基于 Zephyr 自带 Bosch `bme280` 驱动实现。

## 关键结论

- 你当前 PlatformIO/Zephyr 包中没有独立 `bmp280` 驱动。
- Zephyr 的 `bme280` 驱动源码已经内建对 `BMP280` 的识别支持。
- 设备树里应写 `compatible = "bosch,bme280"`。
- 驱动初始化时会读取 `0xD0` 寄存器识别芯片。
- `BMP280` 的常见 `chip id` 是 `0x58`。
- `GY-BMP280` 常见 I2C 地址是 `0x76`，若 `SDO` 上拉则可能为 `0x77`。

## 接线

| GY-BMP280 | XIAO nRF54LM20A |
|-----------|------------------|
| VIN / VCC | 3V3 |
| GND       | GND |
| SCL       | D5 (P1.7) |
| SDA       | D4 (P1.3) |
| CSB / CS  | 拉高到 3V3，强制 I2C 模式 |
| SDO       | GND = `0x76`，3V3 = `0x77` |

## 资料摘要

- 芯片：Bosch BMP280
- 功能：温度、气压
- 工作电压：芯片典型 1.71V 到 3.6V；多数 `GY-BMP280` 模块板载稳压，可直接接 3.3V
- I2C 地址：`0x76` 或 `0x77`
- Chip ID 寄存器：`0xD0`
- BMP280 常用 Chip ID：`0x58`
- 压力数据寄存器起始地址：`0xF7`
- 温度数据寄存器包含在 `0xF7` 后续连续寄存器中
- 软复位寄存器：`0xE0`
- 软复位命令：`0xB6`

## 编译

```bash
C:\Users\seeed\.platformio\penv\Scripts\platformio.exe run
```

## 烧录

```bash
C:\Users\seeed\.platformio\penv\Scripts\platformio.exe run --target upload
```

## 串口

```bash
C:\Users\seeed\.platformio\penv\Scripts\platformio.exe device monitor
```

## 预期输出

```text
=== BMP280 Sensor Reading ===
Temperature: 25.123456 C
Pressure: 101.325000 kPa
=============================
```

## 如果模块地址是 0x77

修改文件 `zephyr/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay` 中这一行：

```dts
reg = <0x77>;
```

## 参考资料

- Bosch BMP280 Datasheet:
  [BST-BMP280-DS001](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf)
- Zephyr BME280 Sample:
  [Zephyr sample](https://docs.zephyrproject.org/latest/samples/sensor/bme280/README.html)
