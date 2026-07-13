/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define RED_NODE DT_ALIAS(led1)
#define BLUE_NODE DT_ALIAS(led0)
#define GREEN_NODE DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(RED_NODE, okay) || !DT_NODE_HAS_PROP(RED_NODE, gpios)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(BLUE_NODE, okay) || !DT_NODE_HAS_PROP(BLUE_NODE, gpios)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(GREEN_NODE, okay) || !DT_NODE_HAS_PROP(GREEN_NODE, gpios)
#error "Unsupported board: led2 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(RED_NODE, gpios);
static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(BLUE_NODE, gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(GREEN_NODE, gpios);

static void set_all(int value)
{
	gpio_pin_set_dt(&red, value);
	gpio_pin_set_dt(&blue, value);
	gpio_pin_set_dt(&green, value);
}

int main(void)
{
	if (!gpio_is_ready_dt(&red) || !gpio_is_ready_dt(&blue) ||
	    !gpio_is_ready_dt(&green)) {
		return 0;
	}

	gpio_pin_configure_dt(&red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&blue, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&green, GPIO_OUTPUT_INACTIVE);

	while (1) {
		set_all(0);
		gpio_pin_set_dt(&red, 1);
		k_msleep(200);

		set_all(0);
		gpio_pin_set_dt(&blue, 1);
		k_msleep(200);

		set_all(0);
		gpio_pin_set_dt(&green, 1);
		k_msleep(200);

		set_all(0);
		k_msleep(800);
	}
}
