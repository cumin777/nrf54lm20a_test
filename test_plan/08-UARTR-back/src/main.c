/*
 * 08-UARTR-back: back-pad UART test for XIAO nRF54LM20B.
 *
 * The board exposes a UART on the back pads at P1.11 (TX) / P1.10 (RX), which
 * is uart20. Console (logs) stays on uart21 (stamp-hole serial).
 *
 * Uses poll mode (uart_poll_in/uart_poll_out), matching the proven
 * 07-uart_21_1M sample. NOTE: async mode (CONFIG_UART_ASYNC_API) must stay
 * OFF, otherwise uart_poll_in() returns -ENOTSUP on nrfx UARTE and RX breaks.
 *
 * Behaviour:
 *   - prints a "heartbeat N" line on uart20 (back pads) every 1 s,
 *   - echoes any byte received on uart20 straight back out uart20,
 *   - mirrors received bytes on the console (uart21) for visibility.
 *
 * Wiring: USB-UART adapter -> back pads
 *   adapter RX  <- P1.11 (uart20 TX)
 *   adapter TX  -> P1.10 (uart20 RX)
 *   GND         -- GND
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_back, LOG_LEVEL_INF);

static const struct device *const back_uart = DEVICE_DT_GET(DT_NODELABEL(uart20));

#define HEARTBEAT_INTERVAL_MS 1000

static void back_send(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(back_uart, data[i]);
	}
}

int main(void)
{
	if (!device_is_ready(back_uart)) {
		LOG_ERR("uart20 not ready");
		return 0;
	}
	LOG_INF("Back-pad UART ready: uart20 TX=P1.11 RX=P1.10 @1000000");

	static const uint8_t banner[] =
		"\r\n08-UARTR-back @1M\r\nType bytes to echo\r\n";
	back_send(banner, sizeof(banner) - 1);

	uint32_t beat = 0;
	int64_t last_hb = k_uptime_get();

	while (1) {
		/* Echo any received byte immediately. */
		uint8_t c;
		if (uart_poll_in(back_uart, &c) == 0) {
			uart_poll_out(back_uart, c);
			printk("rx: 0x%02X '%c'\r\n", c,
			       (c >= 0x20 && c < 0x7f) ? c : '.');
		}

		/* Heartbeat on the back-pad UART. */
		int64_t now = k_uptime_get();
		if (now - last_hb >= HEARTBEAT_INTERVAL_MS) {
			last_hb = now;
			beat++;
			char msg[48];
			int n = snprintk(msg, sizeof(msg),
					 "heartbeat %u\r\n", beat);
			back_send((uint8_t *)msg, n);
		}

		k_msleep(10);
	}

	return 0;
}
