# Grove Barometer Sensor (SPA06-003) SPI Sample for XIAO nRF54LM20A

This sample talks to the `Grove - Barometer Sensor (SPA06-003)` over raw SPI
using Zephyr's `spi_dt_spec` API. It is intentionally read-only:

- Reads and verifies chip ID `0x11`
- Issues a soft reset
- Waits for `SENSOR_RDY` and `COEF_RDY`
- Reads calibration coefficients from `0x10..0x24`
- Configures continuous pressure + temperature measurement
- Polls ready bits and prints compensated pressure and temperature

The current overlay sets `spi-max-frequency = 100 kHz` for signal-integrity
diagnostics. This is intentionally slower than normal operation.

## Wiring

Use the XIAO nRF54LM20A hardware SPI bus on `spi23`:

- `VCC` -> `3V3`
- `GND` -> `GND`
- `SCK` -> `D8` (`P1.04`)
- `SDI` -> `D10` (`P1.06`, MOSI)
- `SDO` -> `D9` (`P1.05`, MISO)
- `CSB` -> `D1` (`P1.31`, CS)

## Module Rework

The Seeed Grove module is configured for I2C by default. For SPI mode:

1. Cut the two interface bus selection pads on the back of the module.
2. Use the module's SPI solder pads.
3. Add wires for `SDO` and `CSB`.

Important hardware caveats from the official Seeed PCB:

- The `INTERFACE` solder bridge ties the sensor `CSB` net to `3V3` by default.
  If it is not fully cut, the board stays in I2C mode.
- The `ADDR1` 3-pad bridge is directly on the sensor `SDO` net.
  If it is shorted to the `0x76` side, `SDO` is forced low and SPI reads can
  collapse to all `0x00`.

## Build

```sh
pio run -e seeed-xiao-nrf54lm20a
```

## Flash

```sh
pio run -e seeed-xiao-nrf54lm20a --target upload
```
