/*
 * GPIO high-drive output high for XIAO nRF54LM20B.
 * Holds D0 (P1.00) and D1 (P1.31) HIGH as high-drive outputs.
 * Blinks the onboard green LED (P1.24, active-low) as a running indicator.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dt-bindings/gpio/nordic-nrf-gpio.h>

#define LED_PIN		24	/* onboard green LED, active-low */
#define BLINK_MS	500

int main(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

	if (!device_is_ready(gpio1)) {
		return 0;
	}

	/* D0 = P1.00, D1 = P1.31: high-drive (H0H1) output, driven HIGH. */
	gpio_pin_configure(gpio1, 0,  GPIO_OUTPUT_HIGH | NRF_GPIO_DRIVE_H0H1);
	gpio_pin_configure(gpio1, 31, GPIO_OUTPUT_HIGH | NRF_GPIO_DRIVE_H0H1);

	/* Onboard green LED (active-low) — blink to show the program is running. */
	gpio_pin_configure(gpio1, LED_PIN, GPIO_OUTPUT_HIGH);

	while (1) {
		gpio_pin_toggle(gpio1, LED_PIN);
		k_msleep(BLINK_MS);
	}

	return 0;
}
