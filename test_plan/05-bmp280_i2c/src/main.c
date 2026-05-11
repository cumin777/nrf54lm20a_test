/*
 * BMP280 I2C demo for XIAO nRF54LM20A
 *
 * Zephyr does not expose a standalone BMP280 driver in this SDK package.
 * The Bosch BME280 driver also supports BMP280 and distinguishes the chip
 * by reading register 0xD0 during initialization.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmp280_demo, LOG_LEVEL_INF);

const struct device *const bmp280 = DEVICE_DT_GET_ANY(bosch_bme280);

static const struct device *check_bmp280_device(void)
{
	if (bmp280 == NULL) {
		LOG_ERR("No BMP280/BME280-compatible device found in devicetree");
		return NULL;
	}

	if (!device_is_ready(bmp280)) {
		LOG_ERR("Device \"%s\" is not ready", bmp280->name);
		return NULL;
	}

	LOG_INF("Found sensor device \"%s\"", bmp280->name);
	return bmp280;
}

static void print_sensor_value(const char *label, const struct sensor_value *value,
			       const char *unit)
{
	int32_t frac = value->val2;

	if (frac < 0) {
		frac = -frac;
	}

	printk("%s: %d.%06d %s\n", label, value->val1, frac, unit);
}

int main(void)
{
	const struct device *dev = check_bmp280_device();
	struct sensor_value temp;
	struct sensor_value press;
	int rc;

	if (dev == NULL) {
		return 0;
	}

	LOG_INF("BMP280 I2C demo started");
	LOG_INF("Reading temperature and pressure every 2 seconds");

	while (1) {
		rc = sensor_sample_fetch(dev);
		if (rc < 0) {
			LOG_ERR("sensor_sample_fetch failed: %d", rc);
			k_sleep(K_SECONDS(2));
			continue;
		}

		rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (rc < 0) {
			LOG_ERR("Failed to read temperature: %d", rc);
			k_sleep(K_SECONDS(2));
			continue;
		}

		rc = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
		if (rc < 0) {
			LOG_ERR("Failed to read pressure: %d", rc);
			k_sleep(K_SECONDS(2));
			continue;
		}

		printk("\n=== BMP280 Sensor Reading ===\n");
		print_sensor_value("Temperature", &temp, "C");
		print_sensor_value("Pressure", &press, "kPa");
		printk("=============================\n");

		k_sleep(K_SECONDS(2));
	}

	return 0;
}
