# PY25Q128HA PlatformIO Demo for XIAO nRF54LM20A/B (PY25Q128HA on 20B)

This demo enables the external SPI NOR flash already described by the board support and performs one simple cycle:

1. Erase the first erase page of the external flash.
2. Write 32 bytes of demo data.
3. Read the data back.
4. Print the TX/RX buffers and verify they match.

## Wiring

- `SPI_CS` -> `P2.05`
- `SPI_IO0` -> `P2.02`
- `SPI_IO1` -> `P2.04`
- `SPI_IO2` -> `P2.03`
- `SPI_IO3` -> `P2.00`
- `SPI_CLK` -> `P2.01`

## Files

- `platformio.ini`: PlatformIO environment for `seeed-xiao-nrf54lm20a`
- `src/main.c`: erase/write/read/verify demo based on Zephyr flash API
- `zephyr/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`: enables `py25q128` and binds the requested pins
- `zephyr/prj.conf`: enables SPI, flash, and logging

## Build

```bash
cd py25q128ha-demo
pio run
```

## Flash

```bash
cd py25q128ha-demo
pio run -t upload
```

## Expected log

The serial log should show lines similar to:

```text
[00:00:00.000,000] <inf> py25q128_demo: Flash device: SPI_NOR
[00:00:00.000,000] <inf> py25q128_demo: Erase success
[00:00:00.000,000] <inf> py25q128_demo: Write success: 32 bytes
[00:00:00.000,000] <inf> py25q128_demo: Verify OK
```
