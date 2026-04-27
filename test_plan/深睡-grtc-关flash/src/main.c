/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>

#define GRTC_WAKEUP_DELAY_S 2U
#define BOOT_BLINK_ON_MS 120
#define BOOT_BLINK_OFF_MS 120

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(py25q64));

static void blink_led_once(void)
{
	int rc;

	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	rc = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		return;
	}

	gpio_pin_set_dt(&led, 1);
	k_msleep(BOOT_BLINK_ON_MS);
	gpio_pin_set_dt(&led, 0);
	k_msleep(BOOT_BLINK_OFF_MS);

	/* Disconnect LED pin to prevent GPIO output driver leakage in system off */
	gpio_pin_configure_dt(&led, GPIO_DISCONNECTED);
}

static int print_reset_cause(uint32_t reset_cause)
{
	uint32_t supported;
	int rc;

	rc = hwinfo_get_supported_reset_cause(&supported);
	if (rc < 0 || !(reset_cause & supported)) {
		printf("Reset cause not supported: 0x%08" PRIX32 "\n", reset_cause);
		return -ENOTSUP;
	}

	printf("Raw reset cause: 0x%08" PRIX32 "\n", reset_cause);

	if (reset_cause & RESET_DEBUG) {
		printf("Reset by debugger.\n");
	} else if (reset_cause & RESET_CLOCK) {
		printf("Wakeup from System OFF by GRTC.\n");
	} else if (reset_cause & RESET_LOW_POWER_WAKE) {
		printf("Wakeup from System OFF by GPIO.\n");
	} else {
		printf("Other wake up cause 0x%08" PRIX32 ".\n", reset_cause);
	}

	return 0;
}

/*
 * SPI pin assignments for PY25Q64HA:
 *   P2.05 = CS#    -> OUTPUT HIGH  (keep flash deselected, prevent DPD wake)
 *   P2.00 = HOLD#  -> OUTPUT HIGH  (inactive)
 *   P2.03 = WP#    -> OUTPUT HIGH  (inactive)
 *   P2.01 = SCK    -> OUTPUT LOW   (deterministic level)
 *   P2.02 = MOSI   -> OUTPUT LOW   (deterministic level)
 *   P2.04 = MISO   -> INPUT PULL_DOWN (flash output, pull to known level)
 *
 * Datasheet requires all flash inputs at 0V or Vcc during DPD for 0.2uA typ.
 */
static int configure_spi_pins_for_system_off(void)
{
	const struct device *gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));
	int rc;

	if (!device_is_ready(gpio2)) {
		printf("GPIO2 not ready.\n");
		return -ENODEV;
	}

	/* CS# = HIGH: keep flash deselected */
	rc = gpio_pin_configure(gpio2, 5, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* HOLD# = HIGH: inactive */
	rc = gpio_pin_configure(gpio2, 0, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* WP# = HIGH: inactive */
	rc = gpio_pin_configure(gpio2, 3, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* SCK = LOW */
	rc = gpio_pin_configure(gpio2, 1, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}

	/* MOSI = LOW */
	rc = gpio_pin_configure(gpio2, 2, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}

	/* MISO = input with pull-down */
	rc = gpio_pin_configure(gpio2, 4, GPIO_INPUT | GPIO_PULL_DOWN);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int suspend_external_flash(void)
{
	const struct device *flash_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(py25q64)));
	int rc;

	if (!device_is_ready(flash_dev)) {
		printf("Flash device %s is not ready.\n", flash_dev->name);
		return -ENODEV;
	}

	printf("Flash device: %s\n", flash_dev->name);

	/* Step 1: Suspend flash — spi-nor driver sends DPD (0xB9) automatically */
	printf("Suspending external flash (entering DPD)...\n");
	rc = pm_device_action_run(flash_dev, PM_DEVICE_ACTION_SUSPEND);
	if (rc < 0) {
		printf("Could not suspend external flash (%d)\n", rc);
		return rc;
	}
	printf("External flash suspended.\n");

	/* Step 2: Suspend SPI bus */
	if (device_is_ready(flash_bus)) {
		rc = pm_device_action_run(flash_bus, PM_DEVICE_ACTION_SUSPEND);
		if (rc < 0) {
			printf("Could not suspend SPI bus (%d)\n", rc);
			return rc;
		}
		printf("SPI bus suspended.\n");
	}

	/* Step 3: Drive all SPI GPIOs to deterministic levels */
	rc = configure_spi_pins_for_system_off();
	if (rc < 0) {
		printf("Could not configure SPI pins (%d)\n", rc);
		return rc;
	}
	printf("SPI GPIO pins configured for system_off.\n");

	return 0;
}

int main(void)
{
	int rc;
	uint32_t reset_cause;
	const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	blink_led_once();

	if (!device_is_ready(cons)) {
		printf("%s: device not ready.\n", cons->name);
		return 0;
	}

	printf("\n%s system off demo\n", CONFIG_BOARD);

	rc = hwinfo_get_reset_cause(&reset_cause);
	if (rc < 0) {
		printf("Could not read reset cause (%d)\n", rc);
		return 0;
	}

	print_reset_cause(reset_cause);

	/* Clear reset cause early so next wakeup shows correct cause */
	rc = hwinfo_clear_reset_cause();
	if (rc < 0) {
		printf("Could not clear reset cause (%d)\n", rc);
	}

	/* Suspend external flash before entering system off */
	rc = suspend_external_flash();
	if (rc < 0) {
		printf("Aborting system off because flash did not enter low power.\n");
		return 0;
	}

	rc = z_nrf_grtc_wakeup_prepare((uint64_t)GRTC_WAKEUP_DELAY_S * USEC_PER_SEC);
	if (rc < 0) {
		printf("Unable to prepare GRTC as wakeup source (%d)\n", rc);
		return 0;
	}

	printf("Entering system off; wait %u seconds to restart\n", GRTC_WAKEUP_DELAY_S);

	rc = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	if (rc < 0) {
		printf("Could not suspend console (%d)\n", rc);
		return 0;
	}

	sys_poweroff();

	return 0;
}
