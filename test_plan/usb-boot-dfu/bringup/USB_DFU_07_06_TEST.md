# XIAO nRF54LM20B USB DFU test notes

This document records the verified USB DFU flow:

- Baseline firmware: `07-usb-loader-board-cdc`
- Update firmware: `06-usb-dfu-transfer-update`
- Bootloader path: MCUboot firmware loader mode, USB CDC ACM + MCUmgr

## Environment

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$sdk = 'D:\workspace\ncs\v3.3.0'
$boardRoot = 'D:\workspace\xiao_nrf54lm20b\platform-seeedboards\zephyr'
$board = 'xiao_nrf54lm20b/nrf54lm20b/cpuapp'
```

Device programming and KMU provisioning use the toolchain nrfutil home:

```powershell
$env:NRFUTIL_HOME = "$tc\nrfutil\home"
```

USB MCUmgr commands use the separate nrfutil home where `mcu-manager` is installed:

```powershell
$env:NRFUTIL_HOME = 'D:\workspace\nrfutil_mcumgr_home'
```

## Firmware paths

Baseline firmware, programmed by J-Link or `nrfutil device program`:

```text
D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex
```

Baseline KMU key file:

```text
D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\keyfile.json
```

Update package uploaded through USB DFU:

```text
D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\dfu_application.zip
```

Update signed binary, if a raw signed image is needed:

```text
D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\06-usb-dfu-transfer-update\zephyr\zephyr.signed.bin
```

## Expected behavior

`07-usb-loader-board-cdc`:

- Normal reset: application runs, `led2` toggles every 250 ms.
- Hold Button 0 during reset: MCUboot enters USB firmware loader mode.
- In firmware loader mode, the application LED does not blink.
- Windows enumerates a Zephyr CDC ACM port, for example:

```text
COM22    USB 串行设备 (COM22)    USB\VID_2FE3&PID_0004&MI_00\...
```

`06-usb-dfu-transfer-update` after DFU:

- Application image version: `0.0.1+0`.
- Application runs with `led1` toggling every 500 ms.
- On the current XIAO board DTS, `led1` is the red LED.

## Program baseline firmware

Use `nrfutil device program`:

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$env:NRFUTIL_HOME = "$tc\nrfutil\home"

& "$tc\nrfutil\bin\nrfutil.exe" device program `
  --firmware D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex `
  --serial-number 69660778 `
  --family nrf54l `
  --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ
```

Or use J-Link Commander:

```text
loadfile D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\merged.hex
r
g
```

## Provision KMU key

The signing validation key is not part of `merged.hex`.
It is provisioned into the chip KMU key slot with `keyfile.json`.

Run this after a full erase/recover, after changing keys, or when bringing up a fresh device:

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$env:NRFUTIL_HOME = "$tc\nrfutil\home"

& "$tc\nrfutil\bin\nrfutil.exe" device x-provision-keys `
  --key-file D:\workspace\build_usb_bringup_07_usb_loader_board_cdc_b\keyfile.json `
  --serial-number 69660778 `
  --family nrf54l

& "$tc\nrfutil\bin\nrfutil.exe" device reset `
  --serial-number 69660778 `
  --family nrf54l
```

If the same KMU key is already provisioned and the chip was not recovered or erased in a way that clears KMU, this step normally does not need to be repeated.

## Enter USB DFU mode

1. Hold XIAO Button 0.
2. Reset the board.
3. Release Button 0 after reset.
4. Confirm the app LED is not blinking.
5. Confirm the Zephyr CDC ACM COM port.

Example COM port check:

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID, Name, PNPDeviceID
```

Expected VID/PID:

```text
VID_2FE3&PID_0004
```

## Check MCUmgr connection

Replace `COM22` with the actual DFU COM port.

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$env:NRFUTIL_HOME = 'D:\workspace\nrfutil_mcumgr_home'

& "$tc\nrfutil\bin\nrfutil.exe" mcu-manager serial image-list `
  --serial-port COM22 `
  --timeout 60
```

This command must return the image list before attempting upload.

## Upload 06 update through USB DFU

```powershell
$tc = 'C:\ncs\toolchains\936afb6332'
$env:NRFUTIL_HOME = 'D:\workspace\nrfutil_mcumgr_home'

& "$tc\nrfutil\bin\nrfutil.exe" mcu-manager serial image-upload `
  --serial-port COM22 `
  --timeout 60 `
  --firmware D:\workspace\build_usb_bringup_06_usb_dfu_transfer_update_b\dfu_application.zip
```

Reset after upload:

```powershell
& "$tc\nrfutil\bin\nrfutil.exe" mcu-manager serial reset `
  --serial-port COM22 `
  --timeout 60
```

After reset, verify that the application behavior changed to the 06 image:

- Red LED (`led1`) toggles every 500 ms.

## Known-good result

The verified result is:

1. `07-usb-loader-board-cdc` boots normally and blinks the original app LED.
2. Holding Button 0 during reset enters USB firmware loader mode.
3. Windows enumerates `USB\VID_2FE3&PID_0004`.
4. `nrfutil mcu-manager serial image-list` responds on the DFU COM port.
5. Uploading `06-usb-dfu-transfer-update` succeeds.
6. After reset, the new app runs with red LED blinking every 500 ms.
