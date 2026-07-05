# 08-UARTR-back：XIAO nRF54LM20B 背部焊盘 UART 测试

20B 板背部焊盘引出一路 UART：`TX=P1.11`、`RX=P1.10`，即 `uart20`。本 sample 在 `uart20` 上做：
- 每 ~1s（无输入时）发一行 `heartbeat N\r\n`；
- 收到任意字节立即原样回发（echo）；
- 收到的字节同时在 console 上打印，便于观察。

console 仍在 `uart21`（邮票孔串口 P1.8/P1.9），所以 `uart20` 可独立用于背部焊盘测试，互不干扰。

## 前置：板级 DT 改动

`uart20` 在 20B 默认 DT 里原本没配。已在 `cumin777/platform-seeedboards` 分支 `seeed_nrf54lm20b_xiao_α_test` 的板级 dtsi 里补上：
- `xiao_nrf54lm20b_nrf54lm20a-pinctrl.dtsi`：新增 `uart20_default`（TX P1.11 / RX P1.10，RX 上拉）和 `uart20_sleep`。
- `xiao_nrf54lm20b_nrf54lm20a-common.dtsi`：新增 `&uart20 { current-speed=<115200>; pinctrl...; }`。
- `nrf54lm20a_cpuapp_common.dtsi`：新增 `&uart20 { status = "okay"; }`。

提交：`board(20b): enable uart20 on back pads P1.11(TX)/P1.10(RX)`（commit 83ef383）。本地编译用的是缓存包 `framework-zephyr-nrf54lm20`，已同步打上同样改动。

## 接线

**背部焊盘 UART（测试对象，uart20）—— 接 USB-UART 转换器：**

| 20B 背部焊盘 | USB-UART 转换器 |
|---|---|
| P1.11 (uart20 TX) | adapter RX |
| P1.10 (uart20 RX) | adapter TX |
| GND | GND |

**邮票孔串口（console，uart21）—— 接另一个 USB-UART 转换器看日志：**

| 20B 邮票孔 | USB-UART 转换器 |
|---|---|
| P1.08 (uart21 TX) | adapter RX |
| P1.09 (uart21 RX) | adapter TX |
| GND | GND |

两个适配器都设 115200。

## 预期

- console（uart21，邮票孔）上看到：
  ```
  Back-pad UART ready: uart20, TX=P1.11 RX=P1.10 @115200
  Heartbeat ~1s on back pads; type bytes on back-pad port to echo
  ```
- 背部焊盘那个串口（uart20）上每秒看到一行 `heartbeat N`。
- 在背部焊盘串口里敲任意字符，会被原样回显；同时 console 打印 `rx: ...`（如果开启日志可见）。

## 编译 / 烧录

```bash
pio run -e seeed-xiao-nrf54lm20b
pio run -e seeed-xiao-nrf54lm20b -t upload
pio device monitor -p <背部焊盘串口> -b 115200
```

## 备注

- console=uart21、测试 UART=uart20，二者独立；`uart20` 不是 console，所以背部焊盘的心跳/回显不会出现在日志里，反之亦然。
- `uart20` 的 RX 用了 async 双缓冲 + msgq，echo 由单独的 worker 线程统一发送，`tx_sem` 保证 tx_buf 不被并发复用。
