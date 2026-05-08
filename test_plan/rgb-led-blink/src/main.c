#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/*
 * RGB LED on XIAO nRF54LM20A:
 *   Blue  -> GPIO1 P22 (led0)
 *   Red   -> GPIO1 P23 (led1)
 *   Green -> GPIO1 P24 (led2)
 *
 * LEDs are active-low: set pin low to turn on, high to turn off.
 */

#define LED_BLUE_PIN  22
#define LED_RED_PIN   23
#define LED_GREEN_PIN 24

#define COLOR_DURATION_MS 500

static const struct device *const gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

struct rgb_color {
	const char *name;
	bool red;
	bool green;
	bool blue;
};

static const struct rgb_color colors[] = {
	{ "Red",     true,  false, false },
	{ "Green",   false, true,  false },
	{ "Blue",    false, false, true  },
	{ "Yellow",  true,  true,  false },
	{ "Cyan",    false, true,  true  },
	{ "Magenta", true,  false, true  },
	{ "White",   true,  true,  true  },
	{ "Off",     false, false, false },
};

#define NUM_COLORS ARRAY_SIZE(colors)

static void set_rgb(bool red, bool green, bool blue)
{
	/* Active-low: GPIO_LOW = LED on, GPIO_HIGH = LED off */
	gpio_pin_set(gpio1, LED_RED_PIN,   red   ? 0 : 1);
	gpio_pin_set(gpio1, LED_GREEN_PIN, green ? 0 : 1);
	gpio_pin_set(gpio1, LED_BLUE_PIN,  blue  ? 0 : 1);
}

int main(void)
{
	if (!device_is_ready(gpio1)) {
		return 0;
	}

	gpio_pin_configure(gpio1, LED_RED_PIN,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(gpio1, LED_GREEN_PIN, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(gpio1, LED_BLUE_PIN,  GPIO_OUTPUT_INACTIVE);

	while (1) {
		for (int i = 0; i < NUM_COLORS; i++) {
			set_rgb(colors[i].red, colors[i].green, colors[i].blue);
			k_msleep(COLOR_DURATION_MS);
		}
	}

	return 0;
}
