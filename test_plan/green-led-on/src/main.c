#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Green LED on XIAO nRF54LM20A: GPIO1 P24 */

static const struct device *const gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

int main(void)
{
	if (!device_is_ready(gpio1)) {
		return 0;
	}

	gpio_pin_configure(gpio1, 22, GPIO_OUTPUT_HIGH); /* Blue OFF */
	gpio_pin_configure(gpio1, 23, GPIO_OUTPUT_HIGH); /* Red OFF */
	gpio_pin_configure(gpio1, 24, GPIO_OUTPUT_LOW);  /* Green ON */

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
