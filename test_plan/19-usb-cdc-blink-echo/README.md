# XIAO nRF54LM20B USB CDC Blink

This sample keeps the app logic intentionally small:

- the application blinks `led0`
- USB CDC ACM remains enabled by the board configuration
- the CDC ACM UART prints a heartbeat once per second after the host opens the COM port

Build with NCS 3.3.0:

```powershell
west build -p always -b xiao_nrf54lm20b/nrf54lm20a/cpuapp D:\workspace\19-usb-cdc-blink-echo -- -DBOARD_ROOT=D:/workspace/xiao_nrf54lm20b/platform-seeedboards/zephyr
```

The app does not use the logging subsystem or echo serial data. The LED should
blink even before the COM port is opened. Heartbeat output starts after the
terminal opens the COM port and asserts DTR.
