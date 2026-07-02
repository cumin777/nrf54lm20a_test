/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/sys/printk.h>

#include "fuel_gauge.h"

#define SLEEP_TIME_MS 1000

#if DT_NODE_EXISTS(DT_NODELABEL(npm1300_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1300_ek_ ## dev))
#elif DT_NODE_EXISTS(DT_NODELABEL(npm1304_ek_pmic))
#define NPM13XX_DEVICE(dev) DEVICE_DT_GET(DT_NODELABEL(npm1304_ek_ ## dev))
#else
#error "neither npm1300 nor npm1304 found in devicetree"
#endif

static const struct device *pmic = NPM13XX_DEVICE(pmic);
static const struct device *charger = NPM13XX_DEVICE(charger);
static volatile bool vbus_connected;

static void log_charge_current_config(void)
{
	struct sensor_value val;
	int ret = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &val);

	if (ret < 0) {
		printk("Could not read configured charge current (err %d)\n", ret);
		return;
	}

	printk("Configured charge current: %lld mA\n",
	       (long long)sensor_value_to_micro(&val) / 1000LL);
}

static bool poll_vbus_status(void)
{
	struct sensor_value val;
	int ret = sensor_attr_get(charger, SENSOR_CHAN_CURRENT,
				  SENSOR_ATTR_UPPER_THRESH, &val);
	if (ret < 0) {
		return false;
	}
	return (val.val1 != 0) || (val.val2 != 0);
}

#if DT_NODE_HAS_PROP(DT_NODELABEL(npm1300_ek_pmic), host_int_gpios)

static void event_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (pins & BIT(NPM13XX_EVENT_VBUS_DETECTED)) {
		printk("Vbus connected\n");
		vbus_connected = true;
	}

	if (pins & BIT(NPM13XX_EVENT_VBUS_REMOVED)) {
		printk("Vbus removed\n");
		vbus_connected = false;
	}
}

static int setup_vbus_callback(void)
{
	static struct gpio_callback event_cb;

	gpio_init_callback(&event_cb, event_callback,
			   BIT(NPM13XX_EVENT_VBUS_DETECTED) |
			   BIT(NPM13XX_EVENT_VBUS_REMOVED));

	return mfd_npm13xx_add_callback(pmic, &event_cb);
}

#define HAS_PMIC_INT 1
#else
#define HAS_PMIC_INT 0
#endif

int main(void)
{
	if (!device_is_ready(pmic)) {
		printk("Pmic device not ready.\n");
		return 0;
	}

	if (!device_is_ready(charger)) {
		printk("Charger device not ready.\n");
		return 0;
	}

	if (fuel_gauge_init(charger) < 0) {
		printk("Could not initialise fuel gauge.\n");
		return 0;
	}

	log_charge_current_config();

#if HAS_PMIC_INT
	if (setup_vbus_callback() != 0) {
		printk("Failed to add pmic callback.\n");
		return 0;
	}
	printk("VBUS interrupt enabled\n");
#else
	printk("No PMIC interrupt pin, using VBUS polling\n");
#endif

	vbus_connected = poll_vbus_status();

	printk("PMIC device ok\n");

	while (1) {
#if !HAS_PMIC_INT
		vbus_connected = poll_vbus_status();
#endif
		fuel_gauge_update(charger, vbus_connected);
		k_msleep(SLEEP_TIME_MS);
	}
}
