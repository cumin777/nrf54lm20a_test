/*
 * Grove Barometer Sensor (SPA06-003) SPI read-only sample for XIAO nRF54LM20A.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spa06_spi, CONFIG_LOG_DEFAULT_LEVEL);

#define SENSOR_NODE DT_NODELABEL(spa06)
#define SPI_OP      (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)
#define SPA06_MAX_XFER 32

static const struct spi_dt_spec sensor_spi = SPI_DT_SPEC_GET(SENSOR_NODE, SPI_OP, 0);

/* SPA06/SPL07 register map */
#define REG_PRS_B2      0x00
#define REG_TMP_B2      0x03
#define REG_PRS_CFG     0x06
#define REG_TMP_CFG     0x07
#define REG_MEAS_CFG    0x08
#define REG_CFG_REG     0x09
#define REG_RESET       0x0C
#define REG_ID          0x0D
#define REG_COEF        0x10

#define SPA06_CHIP_ID           0x11
#define RESET_SOFT_RST          0x09

#define MEAS_CFG_COEF_RDY       BIT(7)
#define MEAS_CFG_SENSOR_RDY     BIT(6)
#define MEAS_CFG_TMP_RDY        BIT(5)
#define MEAS_CFG_PRS_RDY        BIT(4)
#define MEAS_CTRL_CONT_PRS_TMP  0x07

#define CFG_REG_T_SHIFT         BIT(3)
#define CFG_REG_P_SHIFT         BIT(2)

enum spa06_rate {
	SPA06_RATE_1HZ = 0,
	SPA06_RATE_2HZ,
	SPA06_RATE_4HZ,
	SPA06_RATE_8HZ,
	SPA06_RATE_16HZ,
	SPA06_RATE_32HZ,
	SPA06_RATE_64HZ,
	SPA06_RATE_128HZ,
};

enum spa06_osr {
	SPA06_OSR_1 = 0,
	SPA06_OSR_2,
	SPA06_OSR_4,
	SPA06_OSR_8,
	SPA06_OSR_16,
	SPA06_OSR_32,
	SPA06_OSR_64,
	SPA06_OSR_128,
};

struct spa06_cal {
	int16_t c0;
	int16_t c1;
	int32_t c00;
	int32_t c10;
	int16_t c01;
	int16_t c11;
	int16_t c20;
	int16_t c21;
	int16_t c30;
	int16_t c31;
	int16_t c40;
};

static const uint32_t scale_factors[] = {
	524288U,
	1572864U,
	3670016U,
	7864320U,
	253952U,
	516096U,
	1040384U,
	2088960U,
};

static int reg_read_from_spec(const struct spi_dt_spec *spec,
			      uint8_t reg, uint8_t *buf, size_t len)
{
	uint8_t tx[1 + SPA06_MAX_XFER] = { 0 };
	uint8_t rx[1 + SPA06_MAX_XFER] = { 0 };

	if (len > SPA06_MAX_XFER) {
		return -EINVAL;
	}

	tx[0] = reg | 0x80U;

	const struct spi_buf tx_buf = {
		.buf = tx,
		.len = len + 1U,
	};
	const struct spi_buf rx_buf = {
		.buf = rx,
		.len = len + 1U,
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1,
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx_buf,
		.count = 1,
	};

	int ret = spi_transceive_dt(spec, &tx_set, &rx_set);

	if (ret < 0) {
		return ret;
	}

	memcpy(buf, &rx[1], len);
	return 0;
}

static int reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
	return reg_read_from_spec(&sensor_spi, reg, buf, len);
}

static int reg_read_u8(uint8_t reg, uint8_t *value)
{
	return reg_read(reg, value, 1);
}

static int reg_write(uint8_t reg, uint8_t value)
{
	uint8_t tx_data[2] = {
		reg & 0x7FU,
		value,
	};
	const struct spi_buf tx_buf = {
		.buf = tx_data,
		.len = sizeof(tx_data),
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	return spi_write_dt(&sensor_spi, &tx_set);
}

static int32_t spa06_sign_extend(uint32_t raw, uint8_t bits)
{
	return ((int32_t)(raw << (32U - bits))) >> (32U - bits);
}

static int32_t raw_to_s24(const uint8_t *buf)
{
	uint32_t raw = ((uint32_t)buf[0] << 16) |
		       ((uint32_t)buf[1] << 8) |
		       (uint32_t)buf[2];

	return spa06_sign_extend(raw, 24);
}

static int wait_for_bits(uint8_t reg, uint8_t mask, uint8_t expect, int32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (k_uptime_get() <= deadline) {
		uint8_t value = 0;
		int ret = reg_read_u8(reg, &value);

		if (ret < 0) {
			return ret;
		}

		if ((value & mask) == expect) {
			return 0;
		}

		k_msleep(1);
	}

	return -ETIMEDOUT;
}

static int read_coefficients(struct spa06_cal *cal)
{
	uint8_t coef[21];
	int ret = reg_read(REG_COEF, coef, sizeof(coef));

	if (ret < 0) {
		return ret;
	}

	cal->c0 = spa06_sign_extend((((uint16_t)coef[0] << 4) | (coef[1] >> 4)), 12);
	cal->c1 = spa06_sign_extend((((uint16_t)(coef[1] & 0x0F) << 8) | coef[2]), 12);

	cal->c00 = spa06_sign_extend((((uint32_t)coef[3] << 12) |
				     ((uint32_t)coef[4] << 4) |
				     ((uint32_t)coef[5] >> 4)), 20);
	cal->c10 = spa06_sign_extend(((((uint32_t)coef[5] & 0x0F) << 16) |
				     ((uint32_t)coef[6] << 8) |
				     (uint32_t)coef[7]), 20);

	cal->c01 = spa06_sign_extend((((uint16_t)coef[8] << 8) | coef[9]), 16);
	cal->c11 = spa06_sign_extend((((uint16_t)coef[10] << 8) | coef[11]), 16);
	cal->c20 = spa06_sign_extend((((uint16_t)coef[12] << 8) | coef[13]), 16);
	cal->c21 = spa06_sign_extend((((uint16_t)coef[14] << 8) | coef[15]), 16);
	cal->c30 = spa06_sign_extend((((uint16_t)coef[16] << 8) | coef[17]), 16);
	cal->c31 = spa06_sign_extend((((uint16_t)coef[18] << 4) | (coef[19] >> 4)), 12);
	cal->c40 = spa06_sign_extend((((uint16_t)(coef[19] & 0x0F) << 8) | coef[20]), 12);

	return 0;
}

static void log_coefficients(const struct spa06_cal *cal)
{
	LOG_INF("c0=%d c1=%d c00=%d c10=%d",
		cal->c0, cal->c1, cal->c00, cal->c10);
	LOG_INF("c01=%d c11=%d c20=%d c21=%d c30=%d c31=%d c40=%d",
		cal->c01, cal->c11, cal->c20, cal->c21,
		cal->c30, cal->c31, cal->c40);
}

static int configure_sensor(enum spa06_rate prs_rate, enum spa06_osr prs_osr,
			    enum spa06_rate tmp_rate, enum spa06_osr tmp_osr)
{
	uint8_t cfg_reg = 0;
	int ret;

	if (prs_osr > SPA06_OSR_8) {
		cfg_reg |= CFG_REG_P_SHIFT;
	}

	if (tmp_osr > SPA06_OSR_8) {
		cfg_reg |= CFG_REG_T_SHIFT;
	}

	ret = reg_write(REG_PRS_CFG, ((uint8_t)prs_rate << 4) | (uint8_t)prs_osr);
	if (ret < 0) {
		return ret;
	}

	ret = reg_write(REG_TMP_CFG, ((uint8_t)tmp_rate << 4) | (uint8_t)tmp_osr);
	if (ret < 0) {
		return ret;
	}

	ret = reg_write(REG_CFG_REG, cfg_reg);
	if (ret < 0) {
		return ret;
	}

	return reg_write(REG_MEAS_CFG, MEAS_CTRL_CONT_PRS_TMP);
}

static int read_raw_sample(int32_t *raw_temp, int32_t *raw_press)
{
	uint8_t buf[6];
	int ret = reg_read(REG_PRS_B2, buf, sizeof(buf));

	if (ret < 0) {
		return ret;
	}

	*raw_press = raw_to_s24(&buf[0]);
	*raw_temp = raw_to_s24(&buf[3]);

	return 0;
}

static double compensate_temp(int32_t raw_temp, enum spa06_osr tmp_osr,
			      const struct spa06_cal *cal)
{
	double t_sc = raw_temp / (double)scale_factors[tmp_osr];

	return cal->c0 * 0.5 + cal->c1 * t_sc;
}

static double compensate_press(int32_t raw_press, int32_t raw_temp,
			       enum spa06_osr prs_osr, enum spa06_osr tmp_osr,
			       const struct spa06_cal *cal)
{
	double p_sc = raw_press / (double)scale_factors[prs_osr];
	double t_sc = raw_temp / (double)scale_factors[tmp_osr];

	return cal->c00 +
	       p_sc * (cal->c10 +
		       p_sc * (cal->c20 +
			       p_sc * (cal->c30 + cal->c40 * p_sc))) +
	       t_sc * (cal->c01 +
		       p_sc * (cal->c11 +
			       p_sc * (cal->c21 + cal->c31 * p_sc)));
}

static void print_scaled_value(const char *label, const char *unit, double value)
{
	double abs_value = (value < 0.0) ? -value : value;
	int32_t whole = (int32_t)abs_value;
	int32_t frac = (int32_t)((abs_value - (double)whole) * 1000.0 + 0.5);

	if (frac >= 1000) {
		whole += 1;
		frac = 0;
	}

	if (value < 0.0) {
		LOG_INF("%s: -%d.%03d %s", label, whole, frac, unit);
	} else {
		LOG_INF("%s: %d.%03d %s", label, whole, frac, unit);
	}
}

static void dump_spi_mode_diagnostics(void)
{
	static const struct {
		const char *name;
		uint16_t flags;
	} modes[] = {
		{ "mode0", 0U },
		{ "mode1", SPI_MODE_CPHA },
		{ "mode2", SPI_MODE_CPOL },
		{ "mode3", SPI_MODE_CPOL | SPI_MODE_CPHA },
	};

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		struct spi_dt_spec probe = sensor_spi;
		uint8_t id = 0;
		uint8_t meas_cfg = 0;
		int ret_id;
		int ret_meas;

		probe.config.operation &= ~(SPI_MODE_CPOL | SPI_MODE_CPHA);
		probe.config.operation |= modes[i].flags;

		ret_id = reg_read_from_spec(&probe, REG_ID, &id, 1);
		ret_meas = reg_read_from_spec(&probe, REG_MEAS_CFG, &meas_cfg, 1);

		LOG_INF("diag %s: ID ret=%d val=0x%02X, MEAS_CFG ret=%d val=0x%02X",
			modes[i].name, ret_id, id, ret_meas, meas_cfg);
	}

	LOG_WRN("If all modes read 0x00, SDO/MISO is likely held low or the module is still in I2C mode");
	LOG_WRN("Check: INTERFACE pads fully cut so CS is no longer tied to 3V3");
	LOG_WRN("Check: ADDR1 pads are not shorting SDO to GND for 0x76 I2C selection");
	LOG_WRN("Check: SDO wire present, CSB wire present, and D8/D9/D10/D1 wiring");
}

int main(void)
{
	struct spa06_cal cal = { 0 };
	uint8_t chip_id = 0;
	int ret;

	const enum spa06_rate prs_rate = SPA06_RATE_4HZ;
	const enum spa06_osr prs_osr = SPA06_OSR_32;
	const enum spa06_rate tmp_rate = SPA06_RATE_4HZ;
	const enum spa06_osr tmp_osr = SPA06_OSR_1;

	LOG_INF("========== Grove SPA06 SPI Read-Only Sample ==========");
	LOG_INF("SPI frequency: %u Hz",
		(unsigned int)DT_PROP(SENSOR_NODE, spi_max_frequency));

	if (!spi_is_ready_dt(&sensor_spi)) {
		LOG_ERR("SPI bus or CS GPIO is not ready");
		return 0;
	}

	ret = reg_read_u8(REG_ID, &chip_id);
	if (ret < 0) {
		LOG_ERR("Failed to read chip ID: %d", ret);
		return 0;
	}

	LOG_INF("Chip ID: 0x%02X", chip_id);
	if (chip_id != SPA06_CHIP_ID) {
		LOG_ERR("Unexpected chip ID. Expected 0x%02X", SPA06_CHIP_ID);
		dump_spi_mode_diagnostics();
		return 0;
	}

	ret = reg_write(REG_RESET, RESET_SOFT_RST);
	if (ret < 0) {
		LOG_ERR("Soft reset failed: %d", ret);
		return 0;
	}

	k_msleep(12);

	ret = wait_for_bits(REG_MEAS_CFG,
			    MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY,
			    MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY,
			    100);
	if (ret < 0) {
		LOG_ERR("Sensor did not become ready: %d", ret);
		return 0;
	}

	ret = read_coefficients(&cal);
	if (ret < 0) {
		LOG_ERR("Failed to read coefficients: %d", ret);
		return 0;
	}

	log_coefficients(&cal);

	ret = configure_sensor(prs_rate, prs_osr, tmp_rate, tmp_osr);
	if (ret < 0) {
		LOG_ERR("Failed to configure sensor: %d", ret);
		return 0;
	}

	LOG_INF("Configured: pressure=4Hz/32x, temperature=4Hz/1x, SPI mode 3");

	while (true) {
		int32_t raw_temp = 0;
		int32_t raw_press = 0;
		double temp_c;
		double press_pa;

		ret = wait_for_bits(REG_MEAS_CFG,
				    MEAS_CFG_PRS_RDY | MEAS_CFG_TMP_RDY,
				    MEAS_CFG_PRS_RDY | MEAS_CFG_TMP_RDY,
				    1000);
		if (ret < 0) {
			LOG_ERR("Timed out waiting for measurement ready: %d", ret);
			k_msleep(250);
			continue;
		}

		ret = read_raw_sample(&raw_temp, &raw_press);
		if (ret < 0) {
			LOG_ERR("Sample read failed: %d", ret);
			k_msleep(250);
			continue;
		}

		temp_c = compensate_temp(raw_temp, tmp_osr, &cal);
		press_pa = compensate_press(raw_press, raw_temp, prs_osr, tmp_osr, &cal);

		LOG_INF("raw_press=%d raw_temp=%d", raw_press, raw_temp);
		print_scaled_value("Pressure", "Pa", press_pa);
		print_scaled_value("Temperature", "C", temp_c);

		k_msleep(250);
	}

	return 0;
}
