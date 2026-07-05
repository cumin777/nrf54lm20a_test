# BMP280 I2C Demo with Dynamic Speed Sweep (XIAO nRF54LM20x)

驱动 `优信电子 GY-BMP280` 模块，走 I2C，**使用 Zephyr 原始 I2C API**（`i2c_configure` / `i2c_reg_read_byte` / `i2c_transfer`），不依赖 `bme280` sensor 驱动。开机后总线从低到高扫频（100 kHz → 400 kHz → 1 MHz → 3.4 MHz），找到能稳定通信的最高速率后定居，并以该速率周期采样。

## 为什么不再用 bme280 驱动

为了能在运行时用 `i2c_configure()` 动态切速，必须直接持有总线设备句柄；sensor 框架把总线访问封装在驱动内部，无法从外部改速率。因此本 sample 改成裸 I2C。代价是只输出寄存器裸字节（不做温度/气压补偿换算）；如需真实的温度/气压数值，参考 `test_plan/05-i2c-speed` 自行加 BMP280 补偿算法。

## 工作流程

1. 取 `i2c22` 设备，`i2c_get_config()` 打印 DTS 默认速率。
2. 读 `0xD0` chip id（BMP280=0x58，BME280=0x60 均可）。
3. 软复位 + 配置 `CTRL_MEAS=0x27`（normal，温度 x1，气压 x1）、`CONFIG=0x00`。
4. **升频扫速**（升序）：每档 `i2c_configure` 后做 10 次 `0xF7` 起 6 字节读，全成功则记为稳定并继续升档，首次出现失败即停止。
5. 打印 `>>> Fastest stable speed: ... <<<`，把总线 settle 到该速率（全失败则回退 100 kHz）。
6. 周期循环：每 2 s 读 6 字节，打印 `[速率] press/temp raw: xx xx xx xx xx xx`。

> 日志中当前速率在三处可见：开机默认速率、扫频每档结果、settle 行及之后每条采样行前缀。

## 接线

| GY-BMP280 | XIAO nRF54LM20x |
|-----------|------------------|
| VIN / VCC | 3V3 |
| GND       | GND |
| SCL       | D5 (P1.7) |
| SDA       | D4 (P1.3) |
| CSB / CS  | 拉高到 3V3，强制 I2C 模式 |
| SDO       | GND = `0x76`，3V3 = `0x77` |

## 寄存器速查

- Chip ID：`0xD0`（BMP280=`0x58`）
- 软复位：`0xE0` ← `0xB6`
- 状态：`0xF3`（bit0 = im_update）
- `CTRL_MEAS`：`0xF4`；`CONFIG`：`0xF5`
- 数据起始：`0xF7`（气压 3 字节 + 温度 3 字节 = 6 字节，无湿度）

## 编译

```bash
# 只编译 20B
pio run -e seeed-xiao-nrf54lm20b
```

## 烧录 / 串口

```bash
pio run -e seeed-xiao-nrf54lm20b -t upload
pio device monitor -p <COMx 或 /dev/ttyACM0> -b 115200
```

## 预期输出

```text
============================================
  BMP280 I2C demo with dynamic speed sweep
  Target: BMP280 @ 0x76 on i2c22
============================================
I2C device: I2C_22
Default I2C frequency from DTS: 400 kHz

Checking BMP280...
BMP280 found, Chip ID: 0x58
BMP280 initialized

========================================
Sweeping I2C speed (low -> high)
========================================

Trying 100 kHz ... OK (10/10)
Trying 400 kHz ... OK (10/10)
Trying 1 MHz ... OK (10/10)
Trying 3.4 MHz ... NOT SUPPORTED (configure ret=-22)

>>> Fastest stable speed: 1 MHz <<<

Settled at 1 MHz
Periodic raw sampling every 2000 ms
[1 MHz] press/temp raw: 4E 6A A0 80 14 60 (ok)
[1 MHz] press/temp raw: 4E 6A B0 80 14 68 (ok)
```

（3.4 MHz 是否被 TWIM 支持取决于 SoC；若驱动拒绝会打印 NOT SUPPORTED 并回退到 1 MHz。5 MHz Ultra 模式为只写/无 ACK，不适用于读，未加入。）

## 如果模块地址是 0x77

修改 `src/main.c` 顶部的：

```c
#define BMP280_ADDR         0x77
```

## 参考资料

- Bosch BMP280 Datasheet:
  [BST-BMP280-DS001](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf)
- 同仓库动态切速参考实现：`test_plan/05-i2c-speed`
