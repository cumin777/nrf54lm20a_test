#include <errno.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/printk.h>

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(py25q128));

static struct gpio_callback button_cb_data;
static struct k_sem wake_sem;

static volatile uint32_t irq_wake_count;
static volatile bool sleeping = true;

static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	irq_wake_count++;
	sleeping = !sleeping;

	if (!sleeping) {
		k_sem_give(&wake_sem);
	}
}

static int configure_led(void)
{
	if (!gpio_is_ready_dt(&led)) {
		printk("LED device is not ready\n");
		return -ENODEV;
	}

	return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

static int configure_button(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Button device is not ready\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));

	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0) {
		return ret;
	}

	return gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
}

/*
 * SPI pin assignments for PY25Q128HA:
 *   P2.05 = CS#    -> OUTPUT HIGH  (keep flash deselected, prevent DPD wake)
 *   P2.00 = HOLD#  -> OUTPUT HIGH  (inactive)
 *   P2.03 = WP#    -> OUTPUT HIGH  (inactive)
 *   P2.01 = SCK    -> OUTPUT LOW   (deterministic level)
 *   P2.02 = MOSI   -> OUTPUT LOW   (deterministic level)
 *   P2.04 = MISO   -> INPUT PULL_DOWN (flash output, pull to known level)
 *
 * Datasheet requires all flash inputs at 0V or Vcc during DPD for 0.2uA typ.
 */
static int configure_spi_pins_for_sleep(void)
{
	const struct device *gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));
	int rc;

	if (!device_is_ready(gpio2)) {
		printk("GPIO2 not ready.\n");
		return -ENODEV;
	}

	/* CS# = HIGH: keep flash deselected */
	rc = gpio_pin_configure(gpio2, 5, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* HOLD# = HIGH: inactive */
	rc = gpio_pin_configure(gpio2, 0, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* WP# = HIGH: inactive */
	rc = gpio_pin_configure(gpio2, 3, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}

	/* SCK = LOW */
	rc = gpio_pin_configure(gpio2, 1, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}

	/* MOSI = LOW */
	rc = gpio_pin_configure(gpio2, 2, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}

	/* MISO = input with pull-down */
	rc = gpio_pin_configure(gpio2, 4, GPIO_INPUT | GPIO_PULL_DOWN);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int suspend_external_flash(void)
{
	const struct device *flash_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(py25q128)));
	int rc;

	if (!device_is_ready(flash_dev)) {
		printk("Flash device %s is not ready.\n", flash_dev->name);
		return -ENODEV;
	}

	printk("Flash device: %s\n", flash_dev->name);

	/* Step 1: Suspend flash — spi-nor driver sends DPD (0xB9) automatically */
	printk("Suspending external flash (entering DPD)...\n");
	rc = pm_device_action_run(flash_dev, PM_DEVICE_ACTION_SUSPEND);
	if (rc < 0) {
		printk("Could not suspend external flash (%d)\n", rc);
		return rc;
	}
	printk("External flash suspended.\n");

	/* Step 2: Suspend SPI bus */
	if (device_is_ready(flash_bus)) {
		rc = pm_device_action_run(flash_bus, PM_DEVICE_ACTION_SUSPEND);
		if (rc < 0) {
			printk("Could not suspend SPI bus (%d)\n", rc);
			return rc;
		}
		printk("SPI bus suspended.\n");
	}

	/* Step 3: Drive all SPI GPIOs to deterministic levels */
	rc = configure_spi_pins_for_sleep();
	if (rc < 0) {
		printk("Could not configure SPI pins (%d)\n", rc);
		return rc;
	}
	printk("SPI GPIO pins configured for sleep.\n");

	return 0;
}

int main(void)
{
	int ret;
	uint32_t wake_count = 0;
	const struct device *const cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	k_sem_init(&wake_sem, 0, 1);

	ret = configure_led();
	if (ret < 0) {
		printk("Failed to configure LED: %d\n", ret);
		return 0;
	}

	ret = configure_button();
	if (ret < 0) {
		printk("Failed to configure BOOT button: %d\n", ret);
		return 0;
	}

	printk("\n%s shallow sleep demo\n", CONFIG_BOARD);
	printk("BOOT button is %s pin %u\n", button.port->name, button.pin);
	printk("Press BOOT once to wake and stay active\n");
	printk("Press BOOT again to re-enter shallow sleep\n");

	/* Suspend external flash once — this demo never uses it */
	ret = suspend_external_flash();
	if (ret < 0) {
		printk("Warning: flash did not enter low power (%d)\n", ret);
	}

	while (true) {
		if (!sleeping) {
			/* Resume console for printing */
			pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

			gpio_pin_set_dt(&led, 0);
			printk("Active mode, wake count=%u\n", wake_count);

			while (!sleeping) {
				k_msleep(20);
			}

			while (gpio_pin_get_dt(&button) == 0) {
				k_msleep(10);
			}
		}

		gpio_pin_set_dt(&led, 1);
		printk("Entering shallow sleep, wake count=%u\n", wake_count);

		/* Suspend console before sleeping to save power */
		pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

		k_sem_take(&wake_sem, K_FOREVER);

		wake_count = irq_wake_count;
		gpio_pin_set_dt(&led, 1);

		/* Resume console after wakeup */
		pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
		printk("Woke up and latched active mode, wake count=%u\n", wake_count);

		while (gpio_pin_get_dt(&button) == 0) {
			k_msleep(10);
		}
	}

	return 0;
}
