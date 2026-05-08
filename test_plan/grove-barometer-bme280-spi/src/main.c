/*
 * Grove Barometer Sensor (BMP280/BME280) SPI sample for XIAO nRF54LM20A.
 * Uses the official Zephyr BME280 sensor driver.
 *
 * Wiring:
 *   Module VCC  -> 3V3
 *   Module GND  -> GND
 *   Module SCK  -> D8  (P1.04)
 *   Module SDI  -> D10 (P1.06)  MOSI
 *   Module SDO  -> D9  (P1.05)  MISO  (solder pad on PCB)
 *   Module CSB  -> D1  (P1.31)  CS    (solder pad on PCB)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bme280_spi_sample, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme280);

	LOG_INF("========== Grove Barometer SPI Sample (Official Driver) ==========");
	LOG_INF("Board: %s", CONFIG_BOARD);
	LOG_INF("SCK=D8  MISO=D9  MOSI=D10  CS=D1");

	if (dev == NULL) {
		LOG_ERR("No BME280 device found in devicetree");
		return 0;
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("Device \"%s\" is not ready", dev->name);
		LOG_INF("Check driver initialization logs above for errors");
		return 0;
	}

	LOG_INF("Found device \"%s\"", dev->name);

	while (1) {
		struct sensor_value temp, press, humidity;

		int ret = sensor_sample_fetch(dev);
		if (ret < 0) {
			LOG_ERR("sensor_sample_fetch failed: %d", ret);
			k_sleep(K_MSEC(2000));
			continue;
		}

		sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);

		LOG_INF("T: %d.%06d C  P: %d.%06d hPa",
			temp.val1, temp.val2,
			press.val1, press.val2);

		/* Humidity only available on BME280 (chip ID 0x60),
		 * not on BMP280 (chip ID 0x58)
		 */
		ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);
		if (ret == 0) {
			LOG_INF("H: %d.%06d %%", humidity.val1, humidity.val2);
		}

		k_sleep(K_MSEC(2000));
	}

	return 0;
}
