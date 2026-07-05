/*
 * BMP280 I2C demo with dynamic bus speed sweep for XIAO nRF54LM20x.
 *
 * Uses raw Zephyr I2C API (no bme280 sensor driver) so the bus clock can be
 * reconfigured at runtime via i2c_configure(). On boot the bus is swept from
 * the lowest to the highest candidate speed; the fastest speed at which reads
 * stay clean is selected, and the firmware then periodically reads the BMP280
 * data registers at that settled speed.
 *
 * Target: Bosch BMP280 (chip id 0x58). The BME280 id 0x60 is also accepted so
 * a BME280 module works the same, but the data window is 6 bytes (press+temp,
 * no humidity) which matches BMP280.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmp280_i2c, LOG_LEVEL_INF);

/* BMP280 I2C address (SDO low = 0x76, SDO high = 0x77) */
#define BMP280_ADDR         0x76

/* BMP280 register addresses */
#define BMP280_REG_ID       0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_REG_STATUS   0xF3
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG   0xF5
#define BMP280_REG_DATA     0xF7  /* 6 bytes: pressure(3) + temp(3) */

/* Chip IDs */
#define BMP280_CHIP_ID      0x58
#define BME280_CHIP_ID      0x60

/* Number of read probes used to judge stability at each candidate speed. */
#define PROBE_ITERATIONS    10
/* Periodic sampling interval after settling on the fastest stable speed. */
#define SAMPLE_INTERVAL_MS  2000
/* BMP280 data window: press(3) + temp(3). */
#define DATA_LENGTH         6

/* I2C controller node from the overlay. */
#define I2C_NODE DT_NODELABEL(i2c22)

static const struct device *i2c_dev;

/* Candidate bus speeds, swept low -> high. */
struct i2c_speed_config {
	uint32_t speed_const;  /* I2C_SPEED_* constant */
	const char *name;
	uint32_t freq_hz;
};

static const struct i2c_speed_config test_speeds[] = {
	{ I2C_SPEED_STANDARD,  "100 kHz",  100000 },
	{ I2C_SPEED_FAST,      "400 kHz",  400000 },
	{ I2C_SPEED_FAST_PLUS, "1 MHz",   1000000 },
	{ I2C_SPEED_HIGH,      "3.4 MHz", 3400000 },
};

static void print_freq(uint32_t freq)
{
	if (freq >= 1000000) {
		printk("%lu MHz", freq / 1000000);
	} else {
		printk("%lu kHz", freq / 1000);
	}
}

/* Read the chip id and confirm a BMP280/BME280 is on the bus. */
static int bmp280_check_device(void)
{
	uint8_t chip_id;
	int ret;

	ret = i2c_reg_read_byte(i2c_dev, BMP280_ADDR, BMP280_REG_ID, &chip_id);
	if (ret != 0) {
		return ret;
	}

	if (chip_id != BMP280_CHIP_ID && chip_id != BME280_CHIP_ID) {
		printk("Warning: unexpected chip ID 0x%02X (BMP280=0x58, BME280=0x60)\n",
		       chip_id);
		return -1;
	}

	printk("BMP280 found, Chip ID: 0x%02X\n", chip_id);
	return 0;
}

/* Soft-reset and put the sensor into normal mode (temp x1, press x1). */
static int bmp280_init(void)
{
	int ret;
	uint8_t status;

	ret = i2c_reg_write_byte(i2c_dev, BMP280_ADDR, BMP280_REG_RESET, 0xB6);
	if (ret != 0) {
		return ret;
	}

	k_msleep(10);

	/* Wait until NVM calibration data is copied (status.bit0 = im_update). */
	do {
		ret = i2c_reg_read_byte(i2c_dev, BMP280_ADDR, BMP280_REG_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
	} while (status & 0x01);

	/* ctrl_meas: osrs_t=x1, osrs_p=x1, mode=normal -> 0b001 001 11 = 0x27 */
	ret = i2c_reg_write_byte(i2c_dev, BMP280_ADDR, BMP280_REG_CTRL_MEAS, 0x27);
	if (ret != 0) {
		return ret;
	}

	/* config: t_sb=0.5ms, filter off, spi3w off */
	ret = i2c_reg_write_byte(i2c_dev, BMP280_ADDR, BMP280_REG_CONFIG, 0x00);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

/* One burst read of the data window starting at 0xF7. */
static int bmp280_read_data(uint8_t *data, size_t len)
{
	uint8_t reg = BMP280_REG_DATA;
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].buf = &reg;
	msgs[0].len = 1;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_RESTART;

	msgs[1].buf = data;
	msgs[1].len = len;
	msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

	ret = i2c_transfer(i2c_dev, msgs, 2, BMP280_ADDR);
	return ret;
}

/*
 * Sweep candidate speeds from low to high. Returns the index of the fastest
 * speed at which PROBE_ITERATIONS reads all succeed, or -1 if none is stable.
 */
static int find_fastest_stable_speed(void)
{
	int fastest_stable_idx = -1;

	printk("\n");
	printk("========================================\n");
	printk("Sweeping I2C speed (low -> high)\n");
	printk("========================================\n");

	for (int i = 0; i < ARRAY_SIZE(test_speeds); i++) {
		int ret;
		int success = 0;
		int failures = 0;

		printk("\nTrying %s ... ", test_speeds[i].name);

		ret = i2c_configure(i2c_dev, I2C_MODE_CONTROLLER |
				    I2C_SPEED_SET(test_speeds[i].speed_const));
		if (ret != 0) {
			printk("NOT SUPPORTED (configure ret=%d)\n", ret);
			break; /* higher speeds won't be supported either */
		}

		k_msleep(5);

		for (int j = 0; j < PROBE_ITERATIONS; j++) {
			uint8_t buf[DATA_LENGTH];

			ret = bmp280_read_data(buf, DATA_LENGTH);
			if (ret == 0) {
				success++;
			} else {
				failures++;
			}
			k_msleep(1);
		}

		if (failures == 0) {
			printk("OK (%d/%d)\n", success, PROBE_ITERATIONS);
			fastest_stable_idx = i;
			/* keep climbing while the bus stays clean */
		} else {
			printk("UNSTABLE (%d failures)\n", failures);
			break; /* stop at first unstable speed */
		}
	}

	printk("\n");
	if (fastest_stable_idx >= 0) {
		printk(">>> Fastest stable speed: %s <<<\n",
		       test_speeds[fastest_stable_idx].name);
	} else {
		printk(">>> No stable speed found! <<<\n");
	}

	return fastest_stable_idx;
}

int main(void)
{
	uint8_t buf[DATA_LENGTH];
	uint32_t current_freq;
	uint32_t loop_count = 0;
	int ret;
	int settle_idx;

	printk("\n");
	printk("============================================\n");
	printk("  BMP280 I2C demo with dynamic speed sweep\n");
	printk("  Target: BMP280 @ 0x%02X on i2c22\n", BMP280_ADDR);
	printk("============================================\n");

	i2c_dev = DEVICE_DT_GET(I2C_NODE);
	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		printk("Error: I2C device not ready!\n");
		return -1;
	}
	printk("I2C device: %s\n", i2c_dev->name);

	/* Report the speed the bus comes up with from the DTS. */
	ret = i2c_get_config(i2c_dev, &current_freq);
	if (ret == 0) {
		printk("Default I2C frequency from DTS: ");
		print_freq(current_freq);
		printk("\n");
	} else {
		printk("Could not read current I2C frequency\n");
	}

	printk("\nChecking BMP280...\n");
	ret = bmp280_check_device();
	if (ret != 0) {
		LOG_ERR("BMP280 not found or not responsive");
		printk("Error: BMP280 not found! Check wiring "
		       "(SDA=P1.03, SCL=P1.07, addr=0x76).\n");
		return -1;
	}

	ret = bmp280_init();
	if (ret != 0) {
		LOG_ERR("BMP280 init failed: %d", ret);
		printk("Error: BMP280 init failed!\n");
		return -1;
	}
	printk("BMP280 initialized\n");

	/* Sweep and pick the fastest stable speed. */
	settle_idx = find_fastest_stable_speed();

	if (settle_idx < 0) {
		/* Fall back to the lowest speed so sampling can still run. */
		settle_idx = 0;
		printk("Falling back to %s for periodic sampling\n",
		       test_speeds[settle_idx].name);
	}

	/* Re-apply the chosen speed so the bus is in a known state. */
	ret = i2c_configure(i2c_dev, I2C_MODE_CONTROLLER |
			    I2C_SPEED_SET(test_speeds[settle_idx].speed_const));
	if (ret != 0) {
		LOG_ERR("Failed to settle speed: %d", ret);
	}
	printk("\nSettled at %s\n", test_speeds[settle_idx].name);
	printk("Periodic raw sampling every %d ms\n", SAMPLE_INTERVAL_MS);

	while (1) {
		ret = bmp280_read_data(buf, DATA_LENGTH);

		printk("[%s] press/temp raw: ", test_speeds[settle_idx].name);
		if (ret == 0) {
			for (int i = 0; i < DATA_LENGTH; i++) {
				printk("%02X ", buf[i]);
			}
			printk("(ok)\n");
		} else {
			printk("read failed (%d)\n", ret);
			LOG_WRN("read failed at loop %u: %d", loop_count, ret);
		}

		loop_count++;
		k_msleep(SAMPLE_INTERVAL_MS);
	}

	return 0;
}
