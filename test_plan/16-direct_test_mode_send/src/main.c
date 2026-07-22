/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/pm/device_runtime.h>

#include "transport/dtm_transport.h"

static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void status_led_blink_thread(void)
{
	if (!gpio_is_ready_dt(&status_led)) {
		return;
	}

	if (gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE) != 0) {
		return;
	}

	for (;;) {
		(void)gpio_pin_toggle_dt(&status_led);
		k_sleep(K_MSEC(500));
	}
}

K_THREAD_DEFINE(status_led_blink_thread_id, 512, status_led_blink_thread,
		NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int err;
	union dtm_tr_packet cmd;

	printk("Starting Direct Test Mode sample\n");

#if defined(CONFIG_SOC_SERIES_NRF54HX)
	const struct device *dtm_uart = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(ncs_dtm_uart));

	if (dtm_uart != NULL) {
		int ret = pm_device_runtime_get(dtm_uart);

		if (ret < 0) {
			printk("Failed to get DTM UART runtime PM: %d\n", ret);
		}
	}
#endif /* defined(CONFIG_SOC_SERIES_NRF54HX) */

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
