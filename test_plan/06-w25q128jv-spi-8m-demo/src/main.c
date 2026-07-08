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

static const uint8_t tx_pattern[] = {
	0x57, 0x32, 0x35, 0x51, 0x31, 0x32, 0x38, 0x4A,
	0x56, 0x20, 0x38, 0x4D, 0x48, 0x7A, 0x20, 0x64,
	0x65, 0x6D, 0x6F, 0x20, 0x40, 0x20, 0x30, 0x78,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
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
	/* Lenient match: accept any manufacturer of a W25Q128-compatible part.
	 * Bytes [1..2] = ?? 40 18  -> type 0x40, capacity 0x18 (128 Mbit).
	 * Covers Winbond (EF), GigaDevice (C8), XTX/Boya (0B) and other clones.
	 */
	if (jedec_id[1] == 0x40U && jedec_id[2] == 0x18U) {
		return true;
	}

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
	uint8_t jedec_id[3] = { 0 };
	uint8_t rx_buf[sizeof(tx_pattern)];
	const struct flash_parameters *params;
	struct flash_pages_info page_info;

	LOG_INF("================ W25Q128JV demo ================");
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
		LOG_ERR("Unexpected JEDEC ID, want ?? 40 18 (W25Q128-compatible)");
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

	if (page_info.size < TEST_AREA_SIZE) {
		LOG_ERR("Page size %u is smaller than test area %u",
			(unsigned int)page_info.size, (unsigned int)TEST_AREA_SIZE);
		return -EINVAL;
	}

	ret = flash_erase(flash_dev, demo_offset, page_info.size);
	if (ret != 0) {
		LOG_ERR("flash_erase failed: %d", ret);
		return ret;
	}
	LOG_INF("Erase success");

	ret = flash_write(flash_dev, demo_offset, tx_pattern, sizeof(tx_pattern));
	if (ret != 0) {
		LOG_ERR("flash_write failed: %d", ret);
		return ret;
	}
	LOG_INF("Write success: %u bytes", (unsigned int)sizeof(tx_pattern));

	memset(rx_buf, 0, sizeof(rx_buf));
	ret = flash_read(flash_dev, demo_offset, rx_buf, sizeof(rx_buf));
	if (ret != 0) {
		LOG_ERR("flash_read failed: %d", ret);
		return ret;
	}

	dump_hex("TX", tx_pattern, sizeof(tx_pattern));
	dump_hex("RX", rx_buf, sizeof(rx_buf));

	if (memcmp(tx_pattern, rx_buf, sizeof(tx_pattern)) != 0) {
		LOG_ERR("Verify failed: read data does not match write data");
		return -EIO;
	}

	LOG_INF("Verify OK");
	LOG_INF("Demo finished. Data remains at offset 0x%08X",
		(unsigned int)demo_offset);

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
