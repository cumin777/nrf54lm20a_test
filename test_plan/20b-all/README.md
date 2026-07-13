# XIAO nRF54LM20B 20B All

This sample is the integration baseline for the XIAO nRF54LM20B full-feature
firmware. It starts from the validated USB boot DFU flow:

- MCUboot with signed application validation.
- nRF54L KMU/default key provisioning flow.
- Button 0 enters MCUboot firmware loader mode during reset.
- Firmware loader image exposes USB CDC ACM + MCUmgr.
- Application firmware blinks `led2` every 250 ms.

Build example:

```powershell
cd D:\workspace\ncs\v3.3.0

west build -p always `
  -d D:\workspace\build_20b_all `
  -b xiao_nrf54lm20b/nrf54lm20b/cpuapp `
  D:\workspace\xiao_nrf54lm20b\nrf54lm20b_test\nrf54lm20a_test\test_plan\20b-all `
  -- -DBOARD_ROOT=D:\workspace\xiao_nrf54lm20b\platform-seeedboards\zephyr
```

Primary artifacts:

```text
D:\workspace\build_20b_all\keyfile.json
D:\workspace\build_20b_all\dfu_application.zip
D:\workspace\build_20b_all\20b-all\zephyr\zephyr.signed.bin
D:\workspace\build_20b_all\20b-all\zephyr\zephyr.signed.hex
D:\workspace\build_20b_all\mcuboot\zephyr\zephyr.hex
D:\workspace\build_20b_all\usb_mcumgr\zephyr\zephyr.signed.hex
```

Expected behavior:

- Release Button 0 during reset: the application boots and blinks `led2`.
- Hold Button 0 during reset: MCUboot enters USB firmware loader mode. The
  application LED does not blink, and Windows enumerates a Zephyr CDC ACM COM
  port for MCUmgr DFU.
