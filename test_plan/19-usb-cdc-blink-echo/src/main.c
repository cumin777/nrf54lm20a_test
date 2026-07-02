/*
 * Copyright (c) 2026 Seeed Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(usb_cdc_blink, LOG_LEVEL_INF);

#define LED_NODE DT_ALIAS(led0)
#define CDC_UART_NODE DT_CHOSEN(zephyr_console)
#define BLINK_INTERVAL_MS 250
#define HEARTBEAT_INTERVAL_MS 5000
#define LOOP_SLEEP_MS 50
#define CDC_BAUD_RATE 1000000
#define ECHO_BUF_SIZE 1024
#define UART_CHUNK_SIZE 64

BUILD_ASSERT(DT_NODE_HAS_STATUS(LED_NODE, okay), "Board must define led0");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(CDC_UART_NODE, zephyr_cdc_acm_uart),
	     "Console must be a CDC ACM UART");

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct device *const cdc_uart = DEVICE_DT_GET(CDC_UART_NODE);
static uint8_t echo_buffer[ECHO_BUF_SIZE];
static struct ring_buf echo_ringbuf;
static bool rx_throttled;

static void cdc_uart_irq_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (!rx_throttled && uart_irq_rx_ready(dev)) {
			uint8_t buffer[UART_CHUNK_SIZE];
			size_t len = MIN(ring_buf_space_get(&echo_ringbuf), sizeof(buffer));
			int recv_len;
			int rb_len;

			if (len == 0U) {
				uart_irq_rx_disable(dev);
				rx_throttled = true;
				continue;
			}

			recv_len = uart_fifo_read(dev, buffer, len);
			if (recv_len <= 0) {
				continue;
			}

			rb_len = ring_buf_put(&echo_ringbuf, buffer, recv_len);
			if (rb_len > 0) {
				uart_irq_tx_enable(dev);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buffer[UART_CHUNK_SIZE];
			int rb_len;

			rb_len = ring_buf_get(&echo_ringbuf, buffer, sizeof(buffer));
			if (rb_len == 0) {
				uart_irq_tx_disable(dev);
				continue;
			}

			if (rx_throttled) {
				uart_irq_rx_enable(dev);
				rx_throttled = false;
			}

			(void)uart_fifo_fill(dev, buffer, rb_len);
		}
	}
}

int main(void)
{
	int ret;
	int64_t next_blink;
	int64_t next_heartbeat;
	uint32_t heartbeat_count = 0U;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	if (!device_is_ready(cdc_uart)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return 0;
	}

	ring_buf_init(&echo_ringbuf, sizeof(echo_buffer), echo_buffer);

	printk("USB CDC console started\r\n");
	LOG_INF("USB CDC log backend started");
	LOG_INF("Open the CDC ACM port at %u baud", CDC_BAUD_RATE);

	ret = uart_irq_callback_user_data_set(cdc_uart, cdc_uart_irq_handler, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to set CDC ACM callback: %d", ret);
		return 0;
	}

	uart_irq_rx_enable(cdc_uart);
	LOG_INF("CDC ACM echo enabled");

	next_blink = k_uptime_get() + BLINK_INTERVAL_MS;
	next_heartbeat = k_uptime_get() + HEARTBEAT_INTERVAL_MS;

	while (1) {
		int64_t now = k_uptime_get();

		if (now >= next_blink) {
			(void)gpio_pin_toggle_dt(&led);
			next_blink += BLINK_INTERVAL_MS;
		}

		if (now >= next_heartbeat) {
			LOG_INF("heartbeat %u", heartbeat_count++);
			next_heartbeat += HEARTBEAT_INTERVAL_MS;
		}

		k_msleep(LOOP_SLEEP_MS);
	}
}
