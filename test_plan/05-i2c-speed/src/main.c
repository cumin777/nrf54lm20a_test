/*
 * I2C Speed Test for XIAO nRF54LM20A with BME280
 * Tests various I2C clock speeds and measures actual throughput
 *
 * Copyright (c) 2025 Seeed Studio
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys_clock.h>

LOG_MODULE_REGISTER(i2c_speed_test, LOG_LEVEL_INF);

/* BME280 I2C Address */
#define BME280_ADDR         0x76

/* BME280 Register addresses */
#define BME280_REG_ID       0xD0
#define BME280_REG_RESET    0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS   0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG   0xF5
#define BME280_REG_DATA     0xF7  /* 8 bytes: pressure(3) + temp(3) + humidity(2) */

/* Chip IDs: BME280 = 0x60, BMP280 = 0x58. Both are accepted so the same
 * speed-test firmware runs on either module.
 */
#define BME280_CHIP_ID      0x60
#define BMP280_CHIP_ID      0x58

/* Test parameters */
#define NUM_ITERATIONS      100
#define DATA_LENGTH         8   /* Read 8 bytes (full sensor data) */

/* I2C bus node from overlay */
#define I2C_NODE DT_NODELABEL(i2c22)

static const struct device *i2c_dev;

/* I2C speed configuration */
struct i2c_speed_config {
	uint32_t speed_const;  /* I2C_SPEED_* constant */
	const char *name;
	uint32_t freq_hz;      /* Actual frequency in Hz */
};

/* Nordic nRF54LM20A TWIM supported speeds */
static const struct i2c_speed_config test_speeds[] = {
	{ I2C_SPEED_STANDARD,  "100 kHz (Standard)",  100000 },
	{ I2C_SPEED_FAST,      "400 kHz (Fast)",      400000 },
	{ I2C_SPEED_FAST_PLUS, "1 MHz (Fast Plus)",   1000000 },
};

static void print_freq(uint32_t freq)
{
	if (freq >= 1000000) {
		printk("%d MHz", freq / 1000000);
	} else {
		printk("%d kHz", freq / 1000);
	}
}

/* Check if BME280 is present and responsive */
static int bme280_check_device(void)
{
	uint8_t chip_id;
	int ret;

	ret = i2c_reg_read_byte(i2c_dev, BME280_ADDR, BME280_REG_ID, &chip_id);
	if (ret != 0) {
		return ret;
	}

	if (chip_id != BME280_CHIP_ID && chip_id != BMP280_CHIP_ID) {
		printk("Warning: Unexpected chip ID: 0x%02X (expected 0x60 BME280 / 0x58 BMP280)\n",
		       chip_id);
		return -1;
	}

	printk("Sensor found, Chip ID: 0x%02X (%s)\n", chip_id,
	       chip_id == BME280_CHIP_ID ? "BME280" : "BMP280");
	return 0;
}

/* Initialize BME280 for continuous measurements */
static int bme280_init(void)
{
	int ret;
	uint8_t status;

	/* Soft reset */
	ret = i2c_reg_write_byte(i2c_dev, BME280_ADDR, BME280_REG_RESET, 0xB6);
	if (ret != 0) {
		return ret;
	}

	/* Wait for reset to complete */
	k_msleep(10);

	/* Wait until NVM calibration data is copied */
	do {
		ret = i2c_reg_read_byte(i2c_dev, BME280_ADDR, BME280_REG_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
	} while (status & 0x01);

	/* Configure humidity oversampling x1 */
	ret = i2c_reg_write_byte(i2c_dev, BME280_ADDR, BME280_REG_CTRL_HUM, 0x01);
	if (ret != 0) {
		return ret;
	}

	/* Configure: normal mode, pressure x1, temperature x1 */
	ret = i2c_reg_write_byte(i2c_dev, BME280_ADDR, BME280_REG_CTRL_MEAS, 0x27);
	if (ret != 0) {
		return ret;
	}

	/* Configure: standby 0.5ms, filter off */
	ret = i2c_reg_write_byte(i2c_dev, BME280_ADDR, BME280_REG_CONFIG, 0x00);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

/* Perform a single I2C read transaction */
static int i2c_read_test(uint8_t *data, size_t len)
{
	uint8_t reg = BME280_REG_DATA;
	struct i2c_msg msgs[2];
	int ret;

	/* First message: send register address */
	msgs[0].buf = &reg;
	msgs[0].len = 1;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_RESTART;

	/* Second message: read data */
	msgs[1].buf = data;
	msgs[1].len = len;
	msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

	ret = i2c_transfer(i2c_dev, msgs, 2, BME280_ADDR);
	return ret;
}

/* Run speed test at specified speed */
static void run_speed_test(const struct i2c_speed_config *speed_cfg)
{
	uint8_t data[DATA_LENGTH];
	int ret;
	uint32_t success_count = 0;
	uint32_t fail_count = 0;
	uint64_t start_time, end_time;
	uint64_t total_time_us;
	uint32_t bytes_transferred;

	printk("\n");
	printk("========================================\n");
	printk("Testing: %s\n", speed_cfg->name);
	printk("========================================\n");

	/* Configure I2C speed */
	ret = i2c_configure(i2c_dev, I2C_MODE_CONTROLLER | I2C_SPEED_SET(speed_cfg->speed_const));
	if (ret != 0) {
		LOG_ERR("Failed to configure I2C speed: %d", ret);
		printk("Configuration FAILED (ret=%d) - SKIPPED\n", ret);
		printk("Note: This speed may not be supported by nRF54LM20A\n");
		return;
	}

	printk("I2C speed configured successfully\n");

	/* Give some time for configuration to settle */
	k_msleep(10);

	/* Verify BME280 is still responsive at this speed */
	ret = bme280_check_device();
	if (ret != 0) {
		printk("Device not responsive at %s - SKIPPED\n", speed_cfg->name);
		return;
	}

	/* Warm-up: do a few reads before timing */
	for (int i = 0; i < 10; i++) {
		i2c_read_test(data, DATA_LENGTH);
		k_msleep(1);
	}

	/* Start timing */
	start_time = k_uptime_ticks();

	/* Run test iterations */
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		ret = i2c_read_test(data, DATA_LENGTH);

		if (ret == 0) {
			success_count++;
		} else {
			fail_count++;
			LOG_WRN("Read failed at iteration %d: %d", i, ret);
		}
	}

	end_time = k_uptime_ticks();

	/* Calculate results */
	total_time_us = k_ticks_to_us_floor64(end_time - start_time);
	bytes_transferred = success_count * DATA_LENGTH * 2; /* write reg addr + read data */

	/* Calculate actual throughput */
	if (total_time_us > 0 && success_count > 0) {
		/* Throughput in bytes per second */
		uint64_t throughput = (uint64_t)bytes_transferred * 1000000ULL / total_time_us;

		/* Effective bit rate (including START, ACK bits, etc.)
		 * Each byte transfer: 8 data bits + 1 ACK bit = 9 bits
		 * Plus overhead: START, address+R/W+ACK, reg addr+ACK, restart,
		 *                address+R/W+ACK, data bytes with ACKs, NACK+STOP
		 * Approximate: ~30 bits overhead per transaction */
		uint64_t effective_bitrate = throughput * 9; /* bits per second */

		printk("\n--- Test Results ---\n");
		printk("  Speed Setting:       %s\n", speed_cfg->name);
		printk("  Iterations:          %d\n", NUM_ITERATIONS);
		printk("  Successful:          %u\n", success_count);
		printk("  Failed:              %u\n", fail_count);
		printk("  Total Time:          %llu us\n", total_time_us);
		printk("  Avg per Transaction: %llu us\n", total_time_us / success_count);
		printk("  Bytes Transferred:   %u bytes\n", bytes_transferred);
		printk("  Throughput:          %llu bytes/s\n", throughput);
		printk("  Effective Bit Rate:  %llu kbps (%llu bps)\n",
		       effective_bitrate / 1000, effective_bitrate);

		/* Calculate theoretical vs actual */
		uint64_t theoretical_bytes = (uint64_t)speed_cfg->freq_hz / 9; /* 9 bits per byte */
		uint32_t efficiency = (throughput * 100) / theoretical_bytes;

		printk("  Theoretical Max:     %llu bytes/s\n", theoretical_bytes);
		printk("  Bus Efficiency:      %u%%\n", efficiency);

		if (fail_count > 0) {
			printk("  Status:              *** UNSTABLE *** (%u failures)\n", fail_count);
		} else {
			printk("  Status:              STABLE\n");
		}
	}

	/* Display last reading */
	if (success_count > 0) {
		printk("\nLast sensor data (raw hex): ");
		for (int i = 0; i < DATA_LENGTH; i++) {
			printk("%02X ", data[i]);
		}
		printk("\n");
	}
}

/* Find the fastest stable speed */
static void find_fastest_stable_speed(void)
{
	int fastest_stable_idx = -1;

	printk("\n");
	printk("========================================\n");
	printk("Finding Fastest Stable Speed\n");
	printk("========================================\n");

	for (int i = ARRAY_SIZE(test_speeds) - 1; i >= 0; i--) {
		uint8_t data[DATA_LENGTH];
		int success = 0;
		int failures = 0;
		int ret;

		printk("\nTrying %s... ", test_speeds[i].name);

		ret = i2c_configure(i2c_dev, I2C_MODE_CONTROLLER |
				    I2C_SPEED_SET(test_speeds[i].speed_const));
		if (ret != 0) {
			printk("NOT SUPPORTED\n");
			continue;
		}

		k_msleep(5);

		/* Try 10 reads at this speed */
		for (int j = 0; j < 10; j++) {
			ret = i2c_read_test(data, DATA_LENGTH);
			if (ret == 0) {
				success++;
			} else {
				failures++;
			}
			k_msleep(1);
		}

		if (failures == 0) {
			printk("OK (%d/%d successful)\n", success, 10);
			fastest_stable_idx = i;
			break; /* Found the fastest stable speed */
		} else {
			printk("UNSTABLE (%d failures)\n", failures);
		}
	}

	printk("\n");
	if (fastest_stable_idx >= 0) {
		printk(">>> Fastest stable speed: %s <<<\n", test_speeds[fastest_stable_idx].name);
	} else {
		printk(">>> No stable speed found! <<<\n");
	}
}

int main(void)
{
	uint32_t current_freq;
	int ret;

	printk("\n");
	printk("============================================\n");
	printk("  I2C Speed Test for XIAO nRF54LM20A\n");
	printk("  Target: BME280 @ 0x76\n");
	printk("============================================\n");
	printk("\nNote: nRF54LM20A TWIM supports:\n");
	printk("  - 100 kHz (Standard)\n");
	printk("  - 400 kHz (Fast)\n");
	printk("  - 1 MHz (Fast Plus)\n");
	printk("\n");

	/* Get I2C device */
	i2c_dev = DEVICE_DT_GET(I2C_NODE);
	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		printk("Error: I2C device not ready!\n");
		return -1;
	}

	printk("I2C device: %s\n", i2c_dev->name);

	/* Get current I2C configuration */
	ret = i2c_get_config(i2c_dev, &current_freq);
	if (ret == 0) {
		printk("Default I2C frequency from DTS: ");
		print_freq(current_freq);
		printk("\n");
	} else {
		printk("Could not read current I2C frequency\n");
	}

	/* Check BME280 presence */
	printk("\nChecking BME280...\n");
	ret = bme280_check_device();
	if (ret != 0) {
		LOG_ERR("BME280 not found or not responsive");
		printk("Error: BME280 not found! Please check connections.\n");
		printk("  - SDA: P1.03 (Pin 4)\n");
		printk("  - SCL: P1.07 (Pin 5)\n");
		return -1;
	}

	/* Initialize BME280 */
	ret = bme280_init();
	if (ret != 0) {
		LOG_ERR("BME280 initialization failed: %d", ret);
		printk("Error: BME280 initialization failed!\n");
		return -1;
	}
	printk("BME280 initialized successfully\n");

	/* Find fastest stable speed */
	find_fastest_stable_speed();

	/* Run detailed speed tests */
	for (int i = 0; i < ARRAY_SIZE(test_speeds); i++) {
		run_speed_test(&test_speeds[i]);
		k_msleep(100);
	}

	printk("\n");
	printk("============================================\n");
	printk("  I2C Speed Test Complete\n");
	printk("============================================\n");
	printk("\nSummary:\n");
	printk("  - nRF54LM20A supports up to 1 MHz I2C\n");
	printk("  - BME280 supports up to 3.4 MHz I2C\n");
	printk("  - Actual max stable speed depends on:\n");
	printk("    * Wire length and quality\n");
	printk("    * Pull-up resistor values\n");
	printk("    * Bus capacitance\n");
	printk("\n");

	return 0;
}
