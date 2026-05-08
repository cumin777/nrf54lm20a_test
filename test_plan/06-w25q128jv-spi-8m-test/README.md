# W25Q128JV SPI 8 MHz Crosstalk Test for XIAO nRF54LM20A

This test drives a `W25Q128JV / 25Q128JVS0` over `spi23` and is intended for
SPI crosstalk validation. It performs:

1. Print the configured SPI clock frequency as `8 MHz`.
2. Read and verify the flash JEDEC ID.
3. Erase the first demo sector.
4. Write 32-byte test blocks to multiple addresses (64 slots, total 2048 bytes).
5. Read all test addresses back once to verify initial write success.
6. Continuously read all test addresses and compare with expected data:
   - match: counted as normal
   - mismatch: reported as abnormal with mismatch address and byte value

## Wiring

- `SCK` -> `D8`  (`P1.04`)
- `MISO` -> `D9`  (`P1.05`)
- `MOSI` -> `D10` (`P1.06`)
- `CS` -> `D1` (`P1.31`)

`WP#` and `HOLD#` are not controlled by the MCU in this demo. They must be
held in their inactive high state by the external hardware.

## Files

- `platformio.ini`: PlatformIO environment for `seeed-xiao-nrf54lm20a`
- `src/main.c`: JEDEC ID read, multi-address write, and continuous multi-address verify
- `zephyr/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`: enables the
  `spi23` flash node and its demo partition
- `zephyr/prj.conf`: enables SPI, SPI NOR, flash, and logging

## Build

```bash
cd w25q128jv-spi-8m-test
pio run
```

## Flash

```bash
cd w25q128jv-spi-8m-test
pio run -t upload
```

## Expected Log (Excerpt)

```text
[00:00:00.000,000] <inf> w25q128jv_demo: SPI clock: 8 MHz (8000000 Hz)
[00:00:00.000,000] <inf> w25q128jv_demo: JEDEC ID: EF 40 18
[00:00:00.000,000] <inf> w25q128jv_demo: Erase success
[00:00:00.000,000] <inf> w25q128jv_demo: Write success: slots=64, bytes=2048
[00:00:00.000,000] <inf> w25q128jv_demo: Initial verify OK: slots=64, bytes=2048
[00:00:01.000,000] <inf> w25q128jv_demo: Continuous verify OK, ok=10000 err=0
```
