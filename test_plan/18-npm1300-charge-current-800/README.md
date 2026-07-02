# XIAO nRF54LM20B nPM1300 fuel gauge 800 mA sample

This sample is based on `D:\workspace\npm1300_fuel_gauge_500ma_demo`.
It keeps the original NCS/west fuel gauge sample structure and uses the onboard nPM1300 PMIC on XIAO nRF54LM20B.

The 20b board overlay overrides the charger node to:

```dts
current-microamp = <800000>;
term-microvolt = <4200000>;
vbus-limit-microamp = <1500000>;
```

The firmware prints the configured charge current and fuel-gauge values:

```text
Configured charge current: 800 mA
PMIC device ok
V: 4.101, I: -0.123, T: 23.06, SoC: 93.09, TTE: nan, TTF: nan
```

## Build with NCS SDK 3.3.0

Run from an initialized nRF Connect SDK 3.3.0 environment:

```powershell
cd D:\workspace\xiao_nrf54lm20b\nrf54lm20b_test\nrf54lm20a_test\test_plan\18-npm1300-charge-current-800
west build -p always -b xiao_nrf54lm20b/nrf54lm20a/cpuapp .
```

If you are not already inside the SDK terminal, make sure `ZEPHYR_BASE` points to the 3.3.0 Zephyr tree before building.

## Important files

- `boards/xiao_nrf54lm20b_nrf54lm20a_cpuapp.overlay`: 20b overlay for the onboard nPM1300.
- `src/main.c`: original fuel gauge sample application.
- `src/fuel_gauge.c`: original nRF fuel gauge integration.
