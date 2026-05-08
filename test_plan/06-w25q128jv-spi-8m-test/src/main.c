/*
 * W25Q128JV SPI 8 MHz demo for XIAO nRF54LM20A.
 *
 * Wiring:
 *   W25Q128JV SCK  -> D8  (P1.04)
 *   W25Q128JV MISO -> D9  (P1.05)
 *   W25Q128JV MOSI -> D10 (P1.06)
 *   W25Q128JV CS   -> D1  (P1.31)
 *
 * WP# and HOLD# are assumed to be held high by external hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(w25q128jv_demo, CONFIG_LOG_DEFAULT_LEVEL);

#define FLASH_NODE DT_NODELABEL(w25q128jv)
#define DEMO_PARTITION_NODE DT_NODELABEL(w25q128jv_demo_partition)
#define W25Q128_SPI_OPERATION (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define TEST_AREA_SIZE 4096U
#define TEST_PATTERN_SIZE 32U
#define TEST_SLOT_COUNT 64U
#define TOTAL_TEST_BYTES (TEST_SLOT_COUNT * TEST_PATTERN_SIZE)
#define VERIFY_LOG_INTERVAL 10000U
#define ANOMALY_LOG_INTERVAL_MS 1000U

static const struct device *const flash_dev =
	DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(DEMO_PARTITION_NODE));
static const struct spi_dt_spec flash_spi =
	SPI_DT_SPEC_GET(FLASH_NODE, W25Q128_SPI_OPERATION, 0);
static const off_t demo_offset = DT_REG_ADDR(DEMO_PARTITION_NODE);
static const size_t demo_size = DT_REG_SIZE(DEMO_PARTITION_NODE);
static const uint8_t supported_jedec_ids[][3] = {
	{ 0xEF, 0x40, 0x18 },
	{ 0xC8, 0x40, 0x18 },
};

static const uint8_t tx_pattern_seed[TEST_PATTERN_SIZE] = {
	0x57, 0x32, 0x35, 0x51, 0x31, 0x32, 0x38, 0x4A,
	0x56, 0x20, 0x38, 0x4D, 0x48, 0x7A, 0x20, 0x64,
	0x65, 0x6D, 0x6F, 0x20, 0x40, 0x20, 0x30, 0x78,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
};

enum anomaly_type {
	ANOMALY_NONE = 0,
	ANOMALY_READ_FAIL,
	ANOMALY_DATA_MISMATCH,
};

struct anomaly_latch {
	enum anomaly_type type;
	uint32_t first_ok_count;
	uint32_t first_err_count;
	uint32_t first_uptime_ms;
	uint32_t last_log_uptime_ms;
	int first_read_ret;
	size_t first_mismatch;
	uint32_t first_slot;
	off_t first_addr;
	uint8_t first_expected;
	uint8_t first_actual;
	uint8_t first_rx_snapshot[TEST_PATTERN_SIZE];
};

static void dump_hex(const char *tag, const uint8_t *data, size_t len)
{
	char line[3 * 16 + 1];
	size_t line_len;

	for (size_t offset = 0; offset < len; offset += 16) {
		line_len = 0;
		memset(line, 0, sizeof(line));

		for (size_t i = 0; i < 16 && (offset + i) < len; ++i) {
			line_len += snprintk(&line[line_len], sizeof(line) - line_len,
					     "%02X ", data[offset + i]);
		}

		LOG_INF("%s %04u: %s", tag, (unsigned int)offset, line);
	}
}

static size_t first_mismatch_index(const uint8_t *expected, const uint8_t *actual, size_t len)
{
	for (size_t i = 0; i < len; ++i) {
		if (expected[i] != actual[i]) {
			return i;
		}
	}

	return len;
}

static off_t slot_address(uint32_t slot)
{
	return demo_offset + (off_t)(slot * TEST_PATTERN_SIZE);
}

static void build_pattern_for_slot(uint32_t slot, uint8_t out[TEST_PATTERN_SIZE])
{
	const uint32_t addr32 = (uint32_t)slot_address(slot);

	memcpy(out, tx_pattern_seed, TEST_PATTERN_SIZE);

	/* Encode slot and address to make each test block unique. */
	out[20] = (uint8_t)(slot & 0xFFU);
	out[21] = (uint8_t)((slot >> 8) & 0xFFU);
	out[22] = (uint8_t)(addr32 & 0xFFU);
	out[23] = (uint8_t)((addr32 >> 8) & 0xFFU);
	out[24] = (uint8_t)((addr32 >> 16) & 0xFFU);
	out[25] = (uint8_t)((addr32 >> 24) & 0xFFU);
	out[26] = (uint8_t)(slot ^ 0x5AU);
	out[27] = (uint8_t)(((slot >> 8) ^ 0xA5U) & 0xFFU);
}

static void latch_read_fail(struct anomaly_latch *anomaly, int ret, uint32_t slot, off_t addr,
			    uint32_t ok_count, uint32_t err_count)
{
	if (anomaly->type != ANOMALY_NONE) {
		return;
	}

	anomaly->type = ANOMALY_READ_FAIL;
	anomaly->first_ok_count = ok_count;
	anomaly->first_err_count = err_count;
	anomaly->first_uptime_ms = k_uptime_get_32();
	anomaly->first_read_ret = ret;
	anomaly->first_slot = slot;
	anomaly->first_addr = addr;
	anomaly->last_log_uptime_ms = anomaly->first_uptime_ms;

	LOG_ERR("First anomaly latched: read failed ret=%d at slot=%u addr=0x%08X, t=%u ms, ok=%u err=%u",
		ret, slot, (unsigned int)addr, anomaly->first_uptime_ms, ok_count, err_count);
}

static void latch_data_mismatch(struct anomaly_latch *anomaly, size_t mismatch,
				const uint8_t *expected, const uint8_t *actual,
				uint32_t slot, off_t addr, uint32_t ok_count, uint32_t err_count)
{
	if (anomaly->type != ANOMALY_NONE) {
		return;
	}

	anomaly->type = ANOMALY_DATA_MISMATCH;
	anomaly->first_ok_count = ok_count;
	anomaly->first_err_count = err_count;
	anomaly->first_uptime_ms = k_uptime_get_32();
	anomaly->first_mismatch = mismatch;
	anomaly->first_slot = slot;
	anomaly->first_addr = addr;
	anomaly->first_expected = expected[mismatch];
	anomaly->first_actual = actual[mismatch];
	memcpy(anomaly->first_rx_snapshot, actual, sizeof(anomaly->first_rx_snapshot));
	anomaly->last_log_uptime_ms = anomaly->first_uptime_ms;

	LOG_ERR("First anomaly latched: mismatch at slot=%u +0x%X (abs=0x%08X), exp=%02X got=%02X, t=%u ms, ok=%u err=%u",
		slot, (unsigned int)mismatch, (unsigned int)(addr + (off_t)mismatch),
		anomaly->first_expected, anomaly->first_actual,
		anomaly->first_uptime_ms, ok_count, err_count);
	dump_hex("EXP", expected, TEST_PATTERN_SIZE);
	dump_hex("ACT", anomaly->first_rx_snapshot, sizeof(anomaly->first_rx_snapshot));
}

static void log_latched_anomaly(struct anomaly_latch *anomaly, uint32_t ok_count, uint32_t err_count)
{
	uint32_t now;

	if (anomaly->type == ANOMALY_NONE) {
		return;
	}

	now = k_uptime_get_32();
	if ((now - anomaly->last_log_uptime_ms) < ANOMALY_LOG_INTERVAL_MS) {
		return;
	}

	anomaly->last_log_uptime_ms = now;

	if (anomaly->type == ANOMALY_READ_FAIL) {
		LOG_ERR("Latched anomaly(read-fail): first_ret=%d slot=%u addr=0x%08X at t=%u ms, first ok=%u err=%u, current ok=%u err=%u",
			anomaly->first_read_ret, anomaly->first_uptime_ms,
			anomaly->first_slot, (unsigned int)anomaly->first_addr,
			anomaly->first_ok_count, anomaly->first_err_count,
			ok_count, err_count);
		return;
	}

	LOG_ERR("Latched anomaly(mismatch): first at slot=%u +0x%X abs=0x%08X exp=%02X got=%02X, t=%u ms, first ok=%u err=%u, current ok=%u err=%u",
		anomaly->first_slot,
		(unsigned int)anomaly->first_mismatch,
		(unsigned int)(anomaly->first_addr + (off_t)anomaly->first_mismatch),
		anomaly->first_expected, anomaly->first_actual,
		anomaly->first_uptime_ms,
		anomaly->first_ok_count, anomaly->first_err_count,
		ok_count, err_count);
}

static int read_jedec_id(uint8_t jedec_id[3])
{
	uint8_t cmd = 0x9FU;
	const struct spi_buf tx_buf = {
		.buf = &cmd,
		.len = sizeof(cmd),
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	struct spi_buf rx_buf[2] = {
		{
			.buf = NULL,
			.len = sizeof(cmd),
		},
		{
			.buf = jedec_id,
			.len = 3,
		},
	};
	const struct spi_buf_set rx = {
		.buffers = rx_buf,
		.count = ARRAY_SIZE(rx_buf),
	};

	if (!spi_is_ready_dt(&flash_spi)) {
		LOG_ERR("SPI bus or CS GPIO is not ready");
		return -ENODEV;
	}

	return spi_transceive_dt(&flash_spi, &tx, &rx);
}

static bool jedec_id_supported(const uint8_t jedec_id[3])
{
	for (size_t i = 0; i < ARRAY_SIZE(supported_jedec_ids); ++i) {
		if (memcmp(jedec_id, supported_jedec_ids[i],
			   sizeof(supported_jedec_ids[i])) == 0) {
			return true;
		}
	}

	return false;
}

int main(void)
{
	int ret;
	uint32_t ok_count = 0;
	uint32_t err_count = 0;
	uint32_t slot;
	off_t addr;
	uint8_t jedec_id[3] = { 0 };
	uint8_t expected_buf[TEST_PATTERN_SIZE];
	uint8_t rx_buf[TEST_PATTERN_SIZE];
	const struct flash_parameters *params;
	struct flash_pages_info page_info;
	struct anomaly_latch anomaly = { 0 };

	LOG_INF("========== W25Q128JV SPI Crosstalk test ==========");
	LOG_INF("Board: %s", CONFIG_BOARD);
	LOG_INF("SPI clock: %u MHz (%u Hz)",
		(unsigned int)(DT_PROP(FLASH_NODE, spi_max_frequency) / 1000000U),
		(unsigned int)DT_PROP(FLASH_NODE, spi_max_frequency));
	LOG_INF("Wiring: SCK=D8/P1.04 MISO=D9/P1.05 MOSI=D10/P1.06 CS=D1/P1.31");

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device %s is not ready", flash_dev->name);
		return -ENODEV;
	}

	ret = read_jedec_id(jedec_id);
	if (ret != 0) {
		LOG_ERR("JEDEC ID read failed: %d", ret);
		return ret;
	}

	LOG_INF("Flash device: %s", flash_dev->name);
	LOG_INF("JEDEC ID: %02X %02X %02X", jedec_id[0], jedec_id[1], jedec_id[2]);

	if (!jedec_id_supported(jedec_id)) {
		LOG_ERR("Unexpected JEDEC ID, supported IDs: EF 40 18 or C8 40 18");
		return -ENODEV;
	}

	params = flash_get_parameters(flash_dev);
	LOG_INF("Write block size: %u", params->write_block_size);
	LOG_INF("Erase value: 0x%02X", params->erase_value);
	LOG_INF("Demo partition offset: 0x%08X, size: %u bytes",
		(unsigned int)demo_offset, (unsigned int)demo_size);

	ret = flash_get_page_info_by_offs(flash_dev, demo_offset, &page_info);
	if (ret != 0) {
		LOG_ERR("flash_get_page_info_by_offs failed: %d", ret);
		return ret;
	}

	LOG_INF("Erase page start: 0x%08X, size: %u bytes",
		(unsigned int)page_info.start_offset, (unsigned int)page_info.size);

	if (page_info.size < TEST_AREA_SIZE || page_info.size < TOTAL_TEST_BYTES) {
		LOG_ERR("Page size %u is smaller than test area %u",
			(unsigned int)page_info.size, (unsigned int)TOTAL_TEST_BYTES);
		return -EINVAL;
	}

	ret = flash_erase(flash_dev, demo_offset, page_info.size);
	if (ret != 0) {
		LOG_ERR("flash_erase failed: %d", ret);
		return ret;
	}
	LOG_INF("Erase success");

	for (slot = 0; slot < TEST_SLOT_COUNT; ++slot) {
		addr = slot_address(slot);
		build_pattern_for_slot(slot, expected_buf);
		ret = flash_write(flash_dev, addr, expected_buf, sizeof(expected_buf));
		if (ret != 0) {
			LOG_ERR("flash_write failed: slot=%u addr=0x%08X ret=%d",
				slot, (unsigned int)addr, ret);
			return ret;
		}
	}
	LOG_INF("Write success: slots=%u, bytes=%u", TEST_SLOT_COUNT, (unsigned int)TOTAL_TEST_BYTES);

	for (slot = 0; slot < TEST_SLOT_COUNT; ++slot) {
		addr = slot_address(slot);
		build_pattern_for_slot(slot, expected_buf);
		memset(rx_buf, 0, sizeof(rx_buf));
		ret = flash_read(flash_dev, addr, rx_buf, sizeof(rx_buf));
		if (ret != 0) {
			LOG_ERR("flash_read failed: slot=%u addr=0x%08X ret=%d",
				slot, (unsigned int)addr, ret);
			return ret;
		}
		if (memcmp(expected_buf, rx_buf, sizeof(rx_buf)) != 0) {
			LOG_ERR("Initial verify failed: slot=%u addr=0x%08X", slot, (unsigned int)addr);
			dump_hex("EXP", expected_buf, sizeof(expected_buf));
			dump_hex("ACT", rx_buf, sizeof(rx_buf));
			return -EIO;
		}
	}

	LOG_INF("Initial verify OK: slots=%u, bytes=%u",
		TEST_SLOT_COUNT, (unsigned int)TOTAL_TEST_BYTES);
	LOG_INF("Start continuous read/verify: base=0x%08X slots=%u block=%u bytes",
		(unsigned int)demo_offset, TEST_SLOT_COUNT, TEST_PATTERN_SIZE);

	while (1) {
		for (slot = 0; slot < TEST_SLOT_COUNT; ++slot) {
			addr = slot_address(slot);
			build_pattern_for_slot(slot, expected_buf);
			ret = flash_read(flash_dev, addr, rx_buf, sizeof(rx_buf));
			if (ret != 0) {
				err_count++;
				latch_read_fail(&anomaly, ret, slot, addr, ok_count, err_count);
				log_latched_anomaly(&anomaly, ok_count, err_count);
				continue;
			}

			if (memcmp(expected_buf, rx_buf, sizeof(rx_buf)) == 0) {
				ok_count++;
				if ((ok_count % VERIFY_LOG_INTERVAL) == 0U) {
					LOG_INF("Continuous verify OK, ok=%u err=%u",
						ok_count, err_count);
				}
				log_latched_anomaly(&anomaly, ok_count, err_count);
				continue;
			}

			err_count++;
			size_t mismatch = first_mismatch_index(expected_buf, rx_buf, sizeof(rx_buf));
			latch_data_mismatch(&anomaly, mismatch, expected_buf, rx_buf,
					    slot, addr, ok_count, err_count);
			log_latched_anomaly(&anomaly, ok_count, err_count);
		}
	}

	return 0;
}
