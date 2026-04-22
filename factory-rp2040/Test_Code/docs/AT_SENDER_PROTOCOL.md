# factory-rp2040 AT/串口发送器协议解读

## 1. 范围

本文档基于 `Test_Code/src/Test_Code.ino`，说明 RP2040 治具程序当前的代码逻辑，以及它对下游设备返回数据包/日志格式的实际要求。

关键结论：

- 这份 RP2040 代码同时维护了两套串口协议。
- `Serial2` 连接的是电流/供电脚本模块，使用 `$` 结尾的私有命令协议。
- `Serial1` 连接的是被测 nRF54 固件/控制台，实际期待的是“纯文本日志/CLI 回显”，不是结构化 `+TESTDATA:...` AT 回包。
- 如果要直接对接当前 `factory` 固件的 `+TESTDATA ... OK/ERROR` 格式，这个 RP2040 发送器代码本身还需要改；按当前实现，它并不会解析 `+TESTDATA`。

## 2. 硬件与串口角色

代码中的 3 个串口角色如下：

- `Serial`
  - 调试串口，输出 RP2040 自身日志到 PC。
- `Serial1`
  - 发给 nRF54 被测件。
  - 发送内容是字符串命令，通常通过 `Serial1.println(...)` 自动追加换行。
  - 接收内容是 nRF54 输出的控制台文本、测试日志、CLI 回显。
- `Serial2`
  - 发给外部电流/供电模块。
  - 发送内容通过 `Serial2.write(...)` 原样发送，不自动加换行。
  - 命令普遍以 `$` 结尾。

相关控制引脚：

- `Signal_Control`
  - 控制产品与主板通信通断。
- `BATTERY_SW`
  - 模拟电池电源开关。
- `XIAO_USB_SW`
  - 控制 USB 供电/通信链路。
- `BUTTON_PIN`
  - RP2040 本地按键，按下开始测试，松开触发 reset。

## 3. 主状态机

主循环逻辑在 `loop()` 中：

1. `BUTTON_PIN` 从低到高：
   - `buttonPressed = true`
   - `buttonReleased = false`
   - `testState = 1`
   - `XIAO_USB_SW = HIGH`
   - 开始执行测试流程
2. `BUTTON_PIN` 从高到低：
   - `buttonReleased = true`
   - 调用 `resetSystem()`
   - 清理当前测试状态

`performTest()` 按 `testState` 顺序执行：

- `State 1`
  - `readVoltageAnd3V3Test()`
- `State 2`
  - `readChargingCurrentTest(-100)`
- `State 3`
  - `bt_scan_test()`
  - `runGpioTest()`
  - `ADC_Test()`
- `State 4`
  - `button_test()`

注意：

- 以下函数当前定义了，但没有接入主流程：
  - `readUARTTest()`
  - `readBattryCurrentTest()`
  - `runMicTest()`
  - `imu_test()`
  - `Return_Flash_Sava_result()`
- `case 5` 的 `flash_saved_state` 分支当前也不会自然走到，因为主流程里没有把 `testState` 推进到 5。

## 4. 两个核心解析器

## 4.1 `sendAndParse()`：Serial2 私有供电模块协议

函数签名：

```cpp
bool sendAndParse(const char *command, float &value, const char *expectedUnit,
                  unsigned long timeout, int maxRetries = 3)
```

行为：

- 通过 `Serial2.write(command)` 原样发送命令。
- 在超时内逐行读回，按 `\n` 分隔。
- 成功条件有两类：
  - 收到包含 `ACK` 的行；
  - 或者收到能被 `sscanf(response, "%f%7s", ...)` 解析的数值+单位。

实际格式要求：

- ACK 型：
  - 任意包含 `ACK` 子串的文本即可。
  - 示例：
    - `ACK`
    - `SET OK ACK`
- 数值型：
  - 可解析为 `float + unit`
  - 示例：
    - `-123.4mA`
    - `-123.4 mA`
    - `285uA`

重要代码行为：

- 若 `expectedUnit == nullptr`，只要收到 `ACK` 即成功。
- 若 `expectedUnit != nullptr`，函数理论上想解析数值，但代码里只要收到过 `ACK`，即使后续没有成功解析数值，也会返回成功。
- 这意味着对 `GETCRNTSGL$` / `GETCRNTUASGL$` 来说，若模块只回 `ACK` 不回数值，调用方仍可能误判成功。

## 4.2 `sendAndCheckResponse()`：Serial1 文本匹配协议

函数签名：

```cpp
bool sendAndCheckResponse(const char* command, const char* targetString,
                          unsigned long timeout, int maxRetries,
                          const char* endCommand)
```

行为：

- 用 `Serial1.println(command)` 发送命令。
- 在超时内逐行读回。
- 只要某一行 `strstr(line, targetString) != NULL`，就判定成功。
- 若 `endCommand != NULL`，成功后会立即再发一次 `Serial1.println(endCommand)`。

实际格式要求：

- 它不是“整包精确匹配”，而是“某一行包含目标子串即可”。
- 所以被测件只要能打印包含目标文本的行，就会被判定通过。

## 5. Serial2 协议需求

下表是代码里对电流/供电模块的实际命令与回包要求。

| 命令 | 用途 | 期望响应格式 | 判定逻辑 |
| --- | --- | --- | --- |
| `DFLTMODEON$` | 打开默认模式/打开电池路径 | 任意包含 `ACK` 的行 | `sendAndParse(..., expectedUnit=nullptr)` 成功即通过 |
| `DFLTMODEOFF$` | 关闭默认模式/复位电池模块 | 任意包含 `ACK` 的行 | 同上 |
| `GETCRNTSGL$` | 读取充电电流 | 可解析的 `<float><unit>`，单位必须是 `mA` | 连续 5 次读到 `<= -100mA` 通过 |
| `GETCRNTUASGL$` | 读取休眠电流 | 可解析的 `<float><unit>`，单位必须是 `uA` | 连续 5 次读到 `<= 300uA` 通过 |
| `SINKOFF$` | 关闭 sink | 任意包含 `ACK` 的行 | 发送成功即可 |
| `SUPPLYOFF$` | 关闭 supply | 任意包含 `ACK` 的行 | 发送成功即可 |

代码中还定义了但当前主流程未使用：

- `SINKON$`
- `SUPPLYON$`
- `Sleep`

## 6. Serial1 协议需求

## 6.1 蓝牙初始化/扫描

调用路径：

- `bt_scan_test()`

发送：

- `bt init\r\n`
- `bt scan on\r\n`
- 成功后自动发送 `bt scan off\r\n`

判定要求：

| 发送命令 | 目标子串 | 说明 |
| --- | --- | --- |
| `bt init` | `LMP: version 6.0` | 只要某行包含该子串即通过 |
| `bt scan on` | `[DEVICE]` | 只要某行包含该子串即通过 |

注意：

- `bt_init` / `bt_scan_on` 字符串本身已经带 `\r\n`，而发送时又调用了 `Serial1.println()`，实际线上可能会出现额外空行。

## 6.2 GPIO 回环

调用路径：

- `runGpioTest()`

命令集合：

| 输出命令 | 输入读取命令 | 描述 |
| --- | --- | --- |
| `gpio set gpio1 4 <0/1>` | `gpio get gpio1 6` | Pair 1 |
| `gpio set gpio2 2 <0/1>` | `gpio get gpio1 7` | Pair 2 |
| `gpio set gpio2 4 <0/1>` | `gpio get gpio1 10` | Pair 3 |
| `gpio set gpio2 1 <0/1>` | `gpio get gpio1 11` | Pair 4 |

判定格式：

- `set` 命令：
  - 目标子串是完整命令本身。
  - 例如发送 `gpio set gpio1 4 1` 后，程序期待某行里包含：
    - `gpio set gpio1 4 1`
- `get` 命令：
  - 目标子串只是 `"0"` 或 `"1"`。
  - 这意味着任何包含数字 `0` / `1` 的行都有可能被误判成功。

这是当前实现的真实要求，不是严谨协议。

## 6.3 休眠/深睡

调用路径：

- `readBattryCurrentTest()`

发送：

- `sys off`

期望回包：

- 任意一行包含：
  - `system off`

成功后，RP2040 会：

- 关闭 `Signal_Control`
- 关闭 `XIAO_USB_SW`
- 然后转去 `Serial2` 侧读取休眠电流

额外行为：

- 若休眠电流通过，RP2040 还会主动给 `Serial1` 发一条：
  - `Sleep Current Test Passed!`

## 6.4 麦克风

调用路径：

- `runMicTest()`
- 当前主流程中已被注释，不会自动执行

发送：

- `mic capture 0`

期望回包格式：

```text
audio data Max: <int> Min: <int> Max consecutive: <int>
```

解析规则：

- `Max > 400` 或 `Min < -400`
- 且 `Max consecutive < 5`

示例：

```text
audio data Max: 532 Min: -487 Max consecutive: 2
```

## 6.5 ADC 电池电压

调用路径：

- `ADC_Test()`

前置发送：

- `gpio conf gpio1 15 o`
- `gpio set gpio1 15 1`

采样命令：

- `adc_read get`

期望多行输出格式：

1. 先出现某行包含：

```text
channel 7:
```

2. 随后出现某行能被解析为：

```text
vol = <int> mV
```

程序处理：

- 把 `vol_mV` 乘以 2 再转成伏特：
  - `adcVoltage = vol_mV * 2 / 1000.0`
- 最终通过条件：
  - `3.5V <= adcVoltage <= 4.2V`

## 6.6 IMU

调用路径：

- `imu_test()`
- 当前主流程中已被注释，不会自动执行

发送：

- `imu get`

期望回包格式：

```text
accel data: <float>, <float>, <float>
```

程序只关心第三个值 `accel_z`，通过条件：

- `accel_z > 9`

注意：

- 注释里写的是 `> 9.5`，但代码实际判断是 `> 9`。

## 6.7 按键/复位

调用路径：

- `button_test()`

程序要求在 20 秒内，按顺序观察到下列 3 类文本：

1. 某行包含：

```text
usr button pressed
```

2. 然后某行包含：

```text
usr button released
```

3. 然后某行包含：

```text
Booting nRF Connect SDK
```

只有顺序满足，才判定通过。

## 6.8 遗留 UART / Flash 保存结果路径

以下逻辑存在于代码里，但当前主流程未接入：

### `readUARTTest()`

期待顺序：

1. 某行包含：
   - `Init Ready`
2. 之后某行包含：
   - `Test Passed`
3. 之后某行包含：
   - `Test_Results_Saved`

额外特殊行：

- `MIC_Test Start`
  - 收到后蜂鸣器响一次
- `Wireless Failed`
  - 立即失败

### `Return_Flash_Sava_result()`

在 10 秒内等待某行完全等于：

```text
Test_Results_Saved
```

## 7. 当前主流程真正依赖的返回格式

如果只看 `performTest()` 当前实际启用路径，RP2040 发送器真正依赖的是下面这些格式：

### Serial2

- `ACK`
- `<float>mA` / `<float> mA`

### Serial1

- `LMP: version 6.0`
- `[DEVICE]`
- `gpio set gpioX Y Z`
- 含 `0` / `1` 的 `gpio get` 返回行
- `channel 7:`
- `vol = <int> mV`
- `usr button pressed`
- `usr button released`
- `Booting nRF Connect SDK`

## 8. 与当前 factory 固件 AT 回包的关键不匹配

当前 `factory` 固件已经采用统一 AT 回包：

```text
+TESTDATA:...
OK
```

但这份 RP2040 代码目前：

- 不解析 `+TESTDATA`
- 不解析 `OK` / `ERROR:<reason>` 作为主判据
- 对 `Serial1` 的多数测试项仍依赖 shell/log 风格字符串

因此，如果要让当前 RP2040 发送器直接驱动当前 `factory` 固件，至少有两种方案：

1. 修改 RP2040 发送器
   - 改成解析 `+TESTDATA:<STATE>,ITEM=...`
   - 改成以 `OK` / `ERROR:...` 作为命令结束判据
2. 在 nRF54 固件增加兼容模式
   - 保留当前 `+TESTDATA`
   - 额外补打 RP2040 代码所需的 legacy 文本行

从长期维护看，推荐方案 1。

## 9. 代码层面的风险点

这些风险会直接影响“协议对齐”：

- `sendAndParse()` 在 `expectedUnit != nullptr` 时，只要收到 `ACK` 就可能返回成功，即使没有读到数值。
- `checkGpioValue()` 只检查返回里是否包含 `"0"` 或 `"1"`，极易误判。
- 蓝牙命令字符串自带 `\r\n`，但发送时又用 `println()`，可能重复换行。
- 当前主流程并没有覆盖 `MIC`、`IMU`、`Sleep Current`、`Flash Saved` 这几条遗留路径。

## 10. 建议的后续动作

如果目标是对齐当前 nRF54 `factory` 固件：

1. 先统一 `Serial1` 侧协议为 `+TESTDATA ... OK/ERROR`。
2. 把 RP2040 代码中的 `sendAndCheckResponse()` 类逻辑替换为结构化字段解析。
3. 把当前未接入主流程的 `MIC/IMU/SLEEP/FLASH` 项重新接入，并同步修改判据。
