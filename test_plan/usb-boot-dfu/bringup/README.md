# USB boot DFU bring-up

Run these samples in order. Flash each sample only after erasing the previous
one, and do not continue until its expected result is observed.

1. `01-led-direct`: no MCUboot, no USB, no partition manager. The application
   is linked at address `0x0`, so it proves that the board DTS, power, reset,
   RRAM programming, and RGB LED pins work independently of the boot chain.
2. `02-mcuboot-signed-led`: add MCUboot and the signed application only. This
   proves image signing, partition addresses, and the MCUboot-to-application
   jump.
3. `03-mcuboot-official-validate-led`: align with the official nRF54LM20B
   MCUboot validation path and verify the signed LED application still boots.
4. `04-usb-loader-build-only`: add `fw_loader/usb_mcumgr` and verify the full
   sysbuild image set can be built.
5. `05-usb-loader-button-entry`: make the MCUboot GPIO entrance explicit for
   XIAO Button 0. Expected behavior: no button -> signed LED app blinks;
   hold Button 0 during reset -> USB CDC ACM firmware loader enumerates and
   the LED app does not run.
6. `06-usb-dfu-transfer-update`: build a second signed application image for
   USB MCUmgr upload. Expected behavior after DFU: the app version is
   `0.0.1+0` and the LED behavior changes to `led1` toggling every 500 ms.
7. `07-usb-loader-board-cdc`: bind the firmware loader MCUmgr transport to the
   XIAO board's default CDC ACM node. Expected behavior: the DFU COM port
   responds to `nrfutil mcu-manager serial image-list`.

Later stages should add full MCUmgr DFU transfer and optional buttonless entry
only after the boot-time USB loader path is confirmed.
