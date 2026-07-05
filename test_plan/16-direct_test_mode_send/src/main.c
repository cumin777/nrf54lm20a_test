/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/printk.h>

#include "transport/dtm_transport.h"

#define RFSW_CTL_NODE DT_NODELABEL(rfsw_ctl)

#if DT_NODE_EXISTS(RFSW_CTL_NODE)
static const struct gpio_dt_spec rfsw_gpio = {
	.port = DEVICE_DT_GET(DT_GPIO_CTLR(RFSW_CTL_NODE, enable_gpios)),
	.pin = DT_GPIO_PIN(RFSW_CTL_NODE, enable_gpios),
	.dt_flags = DT_GPIO_FLAGS(RFSW_CTL_NODE, enable_gpios),
};

static void dtm_select_internal_antenna(void)
{
	int ret;

	if (!device_is_ready(rfsw_gpio.port)) {
		printk("RF switch GPIO not ready\n");
		return;
	}

	ret = gpio_pin_configure_dt(&rfsw_gpio, GPIO_OUTPUT);
	if (ret < 0) {
		printk("Failed to configure RF switch GPIO: %d\n", ret);
		return;
	}

	/* xiao_nrf54l15: logical 1 on this active-low GPIO drives a physical low,
	 * which selects the onboard ceramic antenna.
	 */
	ret = gpio_pin_set_dt(&rfsw_gpio, 1);
	if (ret < 0) {
		printk("Failed to select internal antenna: %d\n", ret);
	}
}
#else
static void dtm_select_internal_antenna(void)
{
}
#endif

int main(void)
{
	int err;
	union dtm_tr_packet cmd;

	printk("Starting Direct Test Mode sample\n");
	dtm_select_internal_antenna();

#if defined(CONFIG_SOC_SERIES_NRF54H)
	const struct device *dtm_uart = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(ncs_dtm_uart));

	if (dtm_uart != NULL) {
		int ret = pm_device_runtime_get(dtm_uart);

		if (ret < 0) {
			printk("Failed to get DTM UART runtime PM: %d\n", ret);
		}
	}
#endif /* defined(CONFIG_SOC_SERIES_NRF54H) */

	err = dtm_tr_init();
	if (err) {
		printk("Error initializing DTM transport: %d\n", err);
		return err;
	}

	for (;;) {
		cmd = dtm_tr_get();
		err = dtm_tr_process(cmd);
		if (err) {
			printk("Error processing command: %d\n", err);
			return err;
		}
	}
}
