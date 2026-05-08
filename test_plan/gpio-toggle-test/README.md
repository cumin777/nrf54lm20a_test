# GPIO Toggle Test - AT Command Interface

通过 UART20 串口发送 AT 指令，控制 XIAO nRF54LM20A 的任意 GPIO 引脚进行配置、翻转、脉冲和读取操作，适用于产线 GPIO 功能验证。

## 串口参数

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | None |
| 流控 | None |

## 端口映射

| 端口号 | 设备 |
|--------|------|
| 0 | GPIO0 |
| 1 | GPIO1 |
| 2 | GPIO2 |

引脚范围：0 ~ 31

## AT 指令列表

### AT - 测试连接

```
发送: AT
返回: OK
```

### AT+HELP - 查看帮助

```
发送: AT+HELP
返回: 帮助信息
```

### AT+CFG - 配置引脚模式

```
发送: AT+CFG=<port>,<pin>,<mode>
返回: OK 或 ERROR: ...

参数:
  port  - 端口号 (0~2)
  pin   - 引脚号 (0~31)
  mode  - O=输出(初始低电平), I=输入
```

**示例：**
```
AT+CFG=1,24,O       <- 将 GPIO1 P24 配置为输出
AT+CFG=1,8,I        <- 将 GPIO1 P08 配置为输入
```

### AT+SET - 设置输出电平

```
发送: AT+SET=<port>,<pin>,<value>
返回: OK 或 ERROR: ...

参数:
  port  - 端口号 (0~2)
  pin   - 引脚号 (0~31)
  value - 0=低电平, 1=高电平
```

**示例：**
```
AT+SET=1,24,1       <- GPIO1 P24 输出高电平
AT+SET=1,24,0       <- GPIO1 P24 输出低电平
```

### AT+TOGGLE - 翻转一次

```
发送: AT+TOGGLE=<port>,<pin>
返回: OK 或 ERROR: ...

参数:
  port  - 端口号 (0~2)
  pin   - 引脚号 (0~31)
```

**示例：**
```
AT+TOGGLE=1,24      <- 翻转 GPIO1 P24
```

### AT+PULSE - 连续脉冲翻转

```
发送: AT+PULSE=<port>,<pin>,<ms>,<count>
返回: +PULSE: done (<count> pulses, <ms> ms interval)

参数:
  port   - 端口号 (0~2)
  pin    - 引脚号 (0~31)
  ms     - 电平保持时间，单位毫秒 (最小10ms)
  count  - 脉冲次数 (1~10000)
```

脉冲波形：每个脉冲包含一次高电平 + 一次低电平，周期 = ms × 2。

**示例：**
```
AT+PULSE=1,24,100,5
```

产生 5 个脉冲，每个脉冲周期 200ms (100ms HIGH + 100ms LOW)，总时长 1 秒：

```
    ___     ___     ___     ___     ___
___|   |___|   |___|   |___|   |___|   |___
   100ms  100ms
   <-------> 一个脉冲周期
```

### AT+READ - 读取引脚电平

```
发送: AT+READ=<port>,<pin>
返回: +READ: <0|1> 或 ERROR: ...

参数:
  port  - 端口号 (0~2)
  pin   - 引脚号 (0~31)
```

**示例：**
```
AT+READ=1,24
+READ: 1
```

## 典型测试流程

### 单引脚翻转测试（示波器观测）

```
AT+CFG=1,24,O           <- 配置 P1.24 为输出
OK
AT+SET=1,24,0           <- 确认低电平
OK
AT+SET=1,24,1           <- 确认高电平
OK
AT+PULSE=1,24,100,50    <- 产生 50 个脉冲，用示波器抓取波形
+PULSE: done (50 pulses, 100 ms interval)
```

### 多引脚逐一测试

```
AT+CFG=1,8,O            <- 配置 P1.08
OK
AT+PULSE=1,8,50,100     <- 翻转 P1.08
+PULSE: done (100 pulses, 50 ms interval)
AT+CFG=1,9,O            <- 配置 P1.09
OK
AT+PULSE=1,9,50,100     <- 翻转 P1.09
+PULSE: done (100 pulses, 50 ms interval)
```

### 读取输入引脚

```
AT+CFG=1,3,I            <- 配置 P1.03 为输入
OK
AT+READ=1,3             <- 读取电平
+READ: 1
```

## 注意事项

1. **不要翻转 UART 引脚**：UART20 使用 GPIO0 的 P0.20(TX) / P0.21(RX)，翻转这些引脚会导致串口通信中断
2. **不要翻转正在使用的外设引脚**：如 SPI Flash 的 CS/CLK/IO 引脚
3. **先 CFG 再操作**：建议先执行 `AT+CFG` 配置引脚为输出模式，再执行 SET/TOGGLE/PULSE
4. **PULSE 为阻塞操作**：脉冲期间无法接收新指令，请等待 `+PULSE: done` 返回后再发送下一条指令
5. **大小写不敏感**：所有 AT 指令不区分大小写

## 构建 & 烧录

```bash
cd test_plan/gpio-toggle-test
pio run
pio run -t upload
```

## 串口工具

推荐使用以下任一工具连接：

- **minicom**: `minicom -D /dev/ttyACM0 -b 115200`
- **screen**: `screen /dev/ttyACM0 115200`
- **picocom**: `picocom /dev/ttyACM0 -b 115200`

发送指令时需附加回车换行（`\r\n`），大多数串口工具默认自动添加。
