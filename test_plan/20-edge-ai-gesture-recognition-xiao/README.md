# XIAO nRF54LM20B Edge AI gesture recognition

This is a board port of the official nRF Edge AI
`edge-ai/applications/gesture_recognition` application.  It reuses the
official Axon NPU model and all official inference, Bluetooth LE HID and UI
source files from `D:\workspace\ncs\edge_add_on_2`.

## Hardware adaptation

- Target: `xiao_nrf54lm20b/nrf54lm20b/cpuapp`.
- Onboard IMU: LSM6DS3TR-C on I2C address `0x6a`; it replaces the official DK
  BMI270 driver through `src/imu_lsm6dsl.c`.
- `boards/xiao_nrf54lm20b_nrf54lm20b_cpuapp.overlay` connects the IMU to the
  nPM1300 LDO1 supply.
- The existing board aliases supply `sw0` and the PWM RGB LED channels.

The nRF54LM20B Axon NPU runs the official `nrf54lm20dk/Axon` model.

## Build

Open a terminal configured for toolchain `C:\ncs\toolchains\936afb6332`, then
run from `D:\workspace\ncs\edge_add_on_2`:

```powershell
west build -p always -b xiao_nrf54lm20b/nrf54lm20b/cpuapp `
  D:\workspace\xiao_nrf54lm20b\nrf54lm20b_test\nrf54lm20a_test\test_plan\20-edge-ai-gesture-recognition-xiao `
  --build-dir D:\workspace\build\xiao-gesture-recognition `
  -- -DBOARD_ROOT=D:\workspace\xiao_nrf54lm20b\platform-seeedboards\zephyr `
     -DEXTRA_ZEPHYR_MODULES=D:\workspace\ncs\edge_add_on_2\edge-ai
```

The application starts BLE HID advertising as `XIAO Edge AI Gesture Remote`.
Pair it with a computer, then use the button to switch presentation/music
control mode. Serial logs are output through the XIAO UART21 castellated pins:
P1.08 (TX) and P1.09 (RX), 115200 bit/s, 8N1, no flow control.

## Important model note

The official model was trained using the nRF54LM20 DK sensor board's BMI270.
LSM6DS3TR-C uses the same six physical inputs and Zephyr units, so this port
is suitable for functional evaluation. Gesture accuracy and axis orientation
must be verified on the XIAO; collect XIAO IMU data and retrain/replace the
model for production-quality recognition.
