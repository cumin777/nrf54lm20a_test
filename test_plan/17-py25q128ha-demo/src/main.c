#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(py25q128_demo, CONFIG_LOG_DEFAULT_LEVEL);

#define DEMO_PARTITION_NODE DT_NODELABEL(py25q128_demo_partition)
#define TEST_AREA_SIZE 4096U

static const struct device *const flash_dev =
	DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(DEMO_PARTITION_NODE));
static const off_t demo_offset = DT_REG_ADDR(DEMO_PARTITION_NODE);
static const size_t demo_size = DT_REG_SIZE(DEMO_PARTITION_NODE);

static const uint8_t tx_pattern[] = {
	0x66, 0x77, 0x55, 0x35, 0x51, 0x36, 0x34, 0x48,
	0x41, 0x2D, 0x55, 0x58, 0x48, 0x2D, 0x49, 0x52,
	0x20, 0x64, 0x65, 0x6D, 0x6F, 0x20, 0x40, 0x20,
	0x6F, 0x66, 0x66, 0x73, 0x65, 0x74, 0x20, 0x30,
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

int main(void)
{
	int ret;
	uint8_t rx_buf[sizeof(tx_pattern)];
	const struct flash_parameters *params;
	struct flash_pages_info page_info;

	LOG_INF("================ PY25Q128HA demo ================");
	LOG_INF("Board: %s", CONFIG_BOARD);
	LOG_INF("SPI_CS=P2.05, CLK=P2.01, IO0=P2.02, IO1=P2.04, IO2=P2.03, IO3=P2.00");

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device %s is not ready", flash_dev->name);
		return -ENODEV;
	}

	params = flash_get_parameters(flash_dev);
	LOG_INF("Flash device: %s", flash_dev->name);
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

	LOG_INF("Reading existing content before erase/write:");
	memset(rx_buf, 0, sizeof(rx_buf));
	ret = flash_read(flash_dev, demo_offset, rx_buf, sizeof(rx_buf));
	if (ret != 0) {
		LOG_ERR("flash_read before erase failed: %d", ret);
		return ret;
	}
	dump_hex("OLD", rx_buf, sizeof(rx_buf));

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
	LOG_INF("Demo finished. The device will keep the data at offset 0x%08X",
		(unsigned int)demo_offset);

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
