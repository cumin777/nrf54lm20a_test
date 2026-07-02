/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Ship mode demo: puts the on-board nPM1300 PMIC regulator parent into ship
 * mode (cuts board power to reach the lowest storage/shipping current). On
 * success the board powers off and this never returns; if ship mode fails it
 * prints the error code.
 *
 * The onboard green LED (P1.24, active-low) is held ON while the board is
 * awake; when ship mode cuts power it turns off naturally.
 */

#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define LED_PIN		24	/* onboard green LED, active-low (LOW = ON) */

const struct device *regulator_parent =
	DEVICE_DT_GET(DT_ALIAS(regulator_parent));

int main(void)
{
	int ret;
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

	/* LED on while the board is awake (not yet in ship mode). */
	if (device_is_ready(gpio1)) {
		gpio_pin_configure(gpio1, LED_PIN, GPIO_OUTPUT_LOW);
	}

	if (!device_is_ready(regulator_parent)) {
		printk("%s is not ready\n", regulator_parent->name);
		return 0;
	}

	printk("%s entering ship mode\n", regulator_parent->name);
	k_busy_wait(10000);

	ret = regulator_parent_ship_mode(regulator_parent);
	printk("%s failed to enter ship mode (ret = %i)\n",
	       regulator_parent->name, ret);
	return 0;
}
