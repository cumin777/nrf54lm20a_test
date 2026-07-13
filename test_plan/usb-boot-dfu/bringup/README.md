# USB boot DFU bring-up

Run these samples in order. Flash each sample only after erasing the previous
one, and do not continue until its expected result is observed.

1. `01-led-direct`: no MCUboot, no USB, no partition manager. The application
   is linked at address `0x0`, so it proves that the board DTS, power, reset,
   RRAM programming, and RGB LED pins work independently of the boot chain.
2. `02-mcuboot-signed-led`: add MCUboot and the signed application only. This
   proves image signing, partition addresses, and the MCUboot-to-application
   jump.
3. Add the firmware-loader GPIO entrance without USB transfer.
4. Add `fw_loader/usb_mcumgr` and verify USB enumeration.

The next stage is intentionally not created until the result of stage 1 is
known. It avoids mixing a hardware bring-up failure with a bootloader issue.
