/*
 * GY-BMP280 SPI demo for XIAO nRF54LM20A.
 * Uses Zephyr's built-in Bosch BME280/BMP280 driver.
 *
 * GY-BMP280 pin mapping in SPI mode:
 *   VCC      -> 3V3
 *   GND      -> GND
 *   SCL/SCK  -> D8  (P1.04)
 *   SDA/SDI  -> D10 (P1.06) MOSI
 *   SDO      -> D9  (P1.05) MISO
 *   CSB      -> D1  (P1.31) CS
 *
 * Note:
 *   Zephyr's built-in driver is named "bme280", but it also supports BMP280
 *   chip IDs and exposes temperature/pressure readings through the sensor API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gy_bmp280_spi_sample, CONFIG_LOG_DEFAULT_LEVEL);

#define BMP280_NODE DT_NODELABEL(bmp280)

#if !DT_NODE_HAS_STATUS(BMP280_NODE, okay)
#error "bmp280 devicetree node is not enabled"
#endif

static const struct device *const bmp280 = DEVICE_DT_GET(BMP280_NODE);

static int64_t abs64(int64_t value)
{
	return (value < 0) ? -value : value;
}

int main(void)
{
	LOG_INF("========== GY-BMP280 SPI Demo (Zephyr built-in driver) ==========");
	LOG_INF("Board: %s", CONFIG_BOARD);
	LOG_INF("SPI wiring: SCK=D8  MISO=D9  MOSI=D10  CS=D1");

	if (!device_is_ready(bmp280)) {
		LOG_ERR("Device \"%s\" is not ready", bmp280->name);
		return 0;
	}

	LOG_INF("Found device \"%s\"", bmp280->name);

	while (1) {
		struct sensor_value temp;
		struct sensor_value press;
		int64_t temp_milli_c;
		int64_t press_milli_kpa;
		int64_t press_milli_hpa;
		int ret;

		ret = sensor_sample_fetch(bmp280);
		if (ret < 0) {
			LOG_ERR("sensor_sample_fetch failed: %d", ret);
			k_sleep(K_SECONDS(2));
			continue;
		}

		ret = sensor_channel_get(bmp280, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (ret < 0) {
			LOG_ERR("failed to read temperature: %d", ret);
			k_sleep(K_SECONDS(2));
			continue;
		}

		ret = sensor_channel_get(bmp280, SENSOR_CHAN_PRESS, &press);
		if (ret < 0) {
			LOG_ERR("failed to read pressure: %d", ret);
			k_sleep(K_SECONDS(2));
			continue;
		}

		temp_milli_c = sensor_value_to_milli(&temp);
		press_milli_kpa = sensor_value_to_milli(&press);
		press_milli_hpa = press_milli_kpa * 10;

		LOG_INF("T: %s%d.%03d C  P: %d.%03d hPa",
			(temp_milli_c < 0) ? "-" : "",
			(int)(abs64(temp_milli_c) / 1000),
			(int)(abs64(temp_milli_c) % 1000),
			(int)(press_milli_hpa / 1000),
			(int)(press_milli_hpa % 1000));

		k_sleep(K_SECONDS(2));
	}

	return 0;
}
