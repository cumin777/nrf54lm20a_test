# XIAO nRF54LM20B USB boot DFU

This sample uses MCUboot firmware loader mode with the NCS `usb_mcumgr`
firmware loader image.

Behavior:

- Release Button 0 during reset: MCUboot boots the application and `led0`
  blinks.
- Hold Button 0 during reset: MCUboot enters firmware loader mode. The
  firmware loader exposes USB CDC ACM + MCUmgr and does not blink the LED.
- If no valid application is present, MCUboot also enters firmware loader mode.

RRAM layout:

- `mcuboot`: 28 KB
- `slot0` application: 1780 KB
- `slot1` firmware loader: 116 KB
- `storage`: 16 KB

Build example:

```powershell
cd D:\workspace\ncs\v3.3.0
$westArgs = @(
    'build', '-p', 'always',
    '-d', '../../build_usb_boot_dfu',
    '-b', 'xiao_nrf54lm20b/nrf54lm20a/cpuapp',
    '../../xiao_nrf54lm20b/nrf54lm20b_test/nrf54lm20a_test/test_plan/usb-boot-dfu',
    '--', '-DBOARD_ROOT=../../../../platform-seeedboards/zephyr'
)
west @westArgs
```

The sysbuild domains are flashed in this order:

```text
mcuboot
usb_mcumgr
usb-boot-dfu
```

Upload an application image in firmware loader mode with MCUmgr over the USB
CDC ACM serial port. The DFU package and signed image are generated at:

```text
D:\workspace\build_usb_boot_dfu\dfu_application.zip
D:\workspace\build_usb_boot_dfu\usb-boot-dfu\zephyr\zephyr.signed.bin
```
