/*
 * All-in-One Demo for XIAO nRF54LM20A
 *
 * Sequentially executes in a loop:
 *   1. Microphone recording (3 seconds)
 *   2. Flash read/write test
 *   3. IMU accelerometer + gyroscope read
 *   4. NPM1300 battery parameter read
 *   5. BLE non-connectable advertising (runs in background)
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <hal/nrf_clock.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(all_in_one, LOG_LEVEL_INF);

/* ----------------------------------------------------------------
 * Device handles
 * ---------------------------------------------------------------- */

/* DMIC */
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_ALIAS(dmic20));

/* Regulators */
static const struct device *const power_en_dev = DEVICE_DT_GET(DT_NODELABEL(power_en));
static const struct device *const ldo1_dev = DEVICE_DT_GET(DT_NODELABEL(ldo1_3v3));

/* IMU */
static const struct device *const imu_dev = DEVICE_DT_GET(DT_ALIAS(imu0));

/* NPM1300 charger */
static const struct device *const charger_dev = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));

/* Flash */
#define DEMO_PARTITION_NODE DT_NODELABEL(py25q64_demo_partition)
static const struct device *const flash_dev =
	DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(DEMO_PARTITION_NODE));
static const off_t flash_demo_offset = DT_REG_ADDR(DEMO_PARTITION_NODE);

#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
static const struct device *const wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
#else
static const struct device *const wdt_dev;
#endif

/* ----------------------------------------------------------------
 * DMIC configuration (16 kHz, 16-bit, 100 ms/chunk, 3 s recording)
 * ---------------------------------------------------------------- */

#define RECORD_TIME_S      3
#define SAMPLE_RATE_HZ     16000
#define SAMPLE_BIT_WIDTH   16
#define BYTES_PER_SAMPLE   (SAMPLE_BIT_WIDTH / 8)
#define CHUNK_DURATION_MS  100
#define CHUNK_SIZE_BYTES   (BYTES_PER_SAMPLE * (SAMPLE_RATE_HZ * CHUNK_DURATION_MS) / 1000)
#define CHUNK_COUNT        8
#define TOTAL_CHUNKS       (RECORD_TIME_S * 1000 / CHUNK_DURATION_MS)
#define READ_TIMEOUT_MS    1000

K_MEM_SLAB_DEFINE_STATIC(mic_slab, CHUNK_SIZE_BYTES, CHUNK_COUNT, 4);

static struct pcm_stream_cfg stream_cfg = {
	.pcm_rate = SAMPLE_RATE_HZ,
	.pcm_width = SAMPLE_BIT_WIDTH,
	.block_size = CHUNK_SIZE_BYTES,
	.mem_slab = &mic_slab,
};

static struct dmic_cfg dmic_config = {
	.io = {
		.min_pdm_clk_freq = 1000000,
		.max_pdm_clk_freq = 3500000,
		.min_pdm_clk_dc = 40,
		.max_pdm_clk_dc = 60,
	},
	.streams = &stream_cfg,
	.channel = {
		.req_num_streams = 1,
		.req_num_chan = 1,
	},
};

/* ----------------------------------------------------------------
 * Flash test pattern
 * ---------------------------------------------------------------- */

static const uint8_t flash_tx_pattern[] = {
	0x41, 0x6C, 0x6C, 0x2D, 0x69, 0x6E, 0x2D, 0x4F,
	0x6E, 0x65, 0x20, 0x44, 0x65, 0x6D, 0x6F, 0x20,
	0x54, 0x65, 0x73, 0x74, 0x20, 0x50, 0x61, 0x74,
	0x74, 0x65, 0x72, 0x6E, 0x20, 0x4F, 0x4B, 0x21,
};

/* ----------------------------------------------------------------
 * BLE advertising data (non-connectable)
 * ---------------------------------------------------------------- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* ----------------------------------------------------------------
 * LED (Green: P1.24, active high)
 * ---------------------------------------------------------------- */

#define RGB_LED_PORT   DT_NODELABEL(gpio1)
#define LED_GREEN_PIN  24

static const struct device *gpio1_dev;
static int wdt_channel_id = -1;
static uint32_t cycle_count;
static uint32_t consecutive_cycle_failures;
static struct onoff_client hfclk_hold_cli;
static bool hfclk_held;
static int hfclk_last_err;

#define WDT_TIMEOUT_MS                 12000
#define CYCLE_FAIL_REBOOT_THRESHOLD    3

static void led_toggle(void)
{
	if (gpio1_dev) {
		gpio_pin_toggle(gpio1_dev, LED_GREEN_PIN);
	}
}

static int init_led(void)
{
	gpio1_dev = DEVICE_DT_GET(RGB_LED_PORT);
	if (!device_is_ready(gpio1_dev)) {
		LOG_ERR("LED GPIO device not ready");
		return -ENODEV;
	}
	gpio_pin_configure(gpio1_dev, LED_GREEN_PIN, GPIO_OUTPUT_INACTIVE);
	LOG_INF("LED: Green (P1.24) initialized");
	return 0;
}

/* ----------------------------------------------------------------
 * Charge status bitmasks (nPM13xx BCHGCHARGESTATUS register)
 * ---------------------------------------------------------------- */

#define NPM13XX_CHG_COMPLETE BIT(1)
#define NPM13XX_CHG_TRICKLE  BIT(2)
#define NPM13XX_CHG_CC       BIT(3)
#define NPM13XX_CHG_CV       BIT(4)

static const char *charge_status_str(int32_t status)
{
	if (status & NPM13XX_CHG_COMPLETE) {
		return "Complete";
	} else if (status & NPM13XX_CHG_TRICKLE) {
		return "Trickle";
	} else if (status & NPM13XX_CHG_CC) {
		return "CC";
	} else if (status & NPM13XX_CHG_CV) {
		return "CV";
	}
	return "Idle";
}

/* ----------------------------------------------------------------
 * Helper: sensor_value to float
 * ---------------------------------------------------------------- */

static inline float sv_to_float(const struct sensor_value *val)
{
	return (float)val->val1 + (float)val->val2 / 1000000.0f;
}

/* ================================================================
 * 1. Power initialisation
 * ================================================================ */

static int init_power(void)
{
	int ret;

	if (!device_is_ready(power_en_dev)) {
		LOG_ERR("power_en regulator not ready");
		return -ENODEV;
	}
	if (!device_is_ready(ldo1_dev)) {
		LOG_ERR("ldo1_3v3 regulator not ready");
		return -ENODEV;
	}

	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable power_en: %d", ret);
		return ret;
	}

	ret = regulator_enable(ldo1_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable ldo1_3v3: %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(20));
	LOG_INF("Power: power_en + LDO1 enabled");
	return 0;
}

/* ================================================================
 * 2. Microphone recording (3 seconds)
 * ================================================================ */

static int do_mic_recording(void)
{
	int ret;
	int read_err = 0;
	void *buffer;
	uint32_t size;
	uint32_t total_bytes = 0;

	ret = dmic_configure(dmic_dev, &dmic_config);
	if (ret < 0) {
		LOG_ERR("DMIC configure failed: %d", ret);
		return ret;
	}

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("DMIC start failed: %d", ret);
		return ret;
	}

	/* Discard first chunk (startup noise) */
	ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT_MS);
	if (ret == 0) {
		k_mem_slab_free(&mic_slab, buffer);
	}

	/* Read 30 chunks = 3 seconds */
	for (int i = 0; i < TOTAL_CHUNKS; i++) {
		ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT_MS);
		if (ret < 0) {
			LOG_ERR("DMIC read failed at chunk %d: %d", i, ret);
			read_err = ret;
			break;
		}
		total_bytes += size;
		k_mem_slab_free(&mic_slab, buffer);
	}

	dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
	if (read_err < 0) {
		return read_err;
	}

	LOG_INF("DMIC: recorded %u bytes (%d s)", total_bytes, RECORD_TIME_S);
	return 0;
}

/* ================================================================
 * 3. Flash read/write test
 * ================================================================ */

static int do_flash_test(void)
{
	int ret;
	uint8_t rx_buf[sizeof(flash_tx_pattern)];
	struct flash_pages_info page_info;

	ret = flash_get_page_info_by_offs(flash_dev, flash_demo_offset, &page_info);
	if (ret != 0) {
		LOG_ERR("Flash: get page info failed: %d", ret);
		return ret;
	}

	ret = flash_erase(flash_dev, flash_demo_offset, page_info.size);
	if (ret != 0) {
		LOG_ERR("Flash: erase failed: %d", ret);
		return ret;
	}

	ret = flash_write(flash_dev, flash_demo_offset, flash_tx_pattern,
			  sizeof(flash_tx_pattern));
	if (ret != 0) {
		LOG_ERR("Flash: write failed: %d", ret);
		return ret;
	}

	memset(rx_buf, 0, sizeof(rx_buf));
	ret = flash_read(flash_dev, flash_demo_offset, rx_buf, sizeof(rx_buf));
	if (ret != 0) {
		LOG_ERR("Flash: read failed: %d", ret);
		return ret;
	}

	if (memcmp(flash_tx_pattern, rx_buf, sizeof(flash_tx_pattern)) != 0) {
		LOG_ERR("Flash: verify FAILED");
		return -EIO;
	}

	LOG_INF("Flash: read/write verify OK (%u bytes)", (unsigned int)sizeof(flash_tx_pattern));
	return 0;
}

/* ================================================================
 * 4. IMU data read
 * ================================================================ */

static bool imu_initialized;

static int init_imu(void)
{
	int ret;

	if (!device_is_ready(imu_dev)) {
		ret = device_init(imu_dev);
		if (ret < 0 && ret != -EALREADY) {
			LOG_ERR("IMU init failed: %d", ret);
			return ret;
		}
	}

	if (!device_is_ready(imu_dev)) {
		LOG_ERR("IMU device not ready after init");
		return -ENODEV;
	}

	/* Set sampling frequency to 12.5 Hz */
	struct sensor_value odr = { .val1 = 12, .val2 = 500000 };

	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (ret != 0) {
		LOG_ERR("IMU: set accel ODR failed: %d", ret);
		return ret;
	}

	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (ret != 0) {
		LOG_ERR("IMU: set gyro ODR failed: %d", ret);
		return ret;
	}

	imu_initialized = true;
	LOG_INF("IMU: LSM6DSL initialized, ODR 12.5 Hz");
	return 0;
}

static int do_imu_read(void)
{
	int ret;

	if (!imu_initialized) {
		return -ENODEV;
	}

	struct sensor_value x, y, z;

	ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ);
	if (ret < 0) {
		LOG_ERR("IMU accel fetch failed: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &x);
	if (ret < 0) {
		LOG_ERR("IMU accel x get failed: %d", ret);
		return ret;
	}
	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &y);
	if (ret < 0) {
		LOG_ERR("IMU accel y get failed: %d", ret);
		return ret;
	}
	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &z);
	if (ret < 0) {
		LOG_ERR("IMU accel z get failed: %d", ret);
		return ret;
	}

	LOG_INF("IMU accel x:%.3f y:%.3f z:%.3f m/s^2",
		(double)sv_to_float(&x), (double)sv_to_float(&y),
		(double)sv_to_float(&z));

	ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_GYRO_XYZ);
	if (ret < 0) {
		LOG_ERR("IMU gyro fetch failed: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_X, &x);
	if (ret < 0) {
		LOG_ERR("IMU gyro x get failed: %d", ret);
		return ret;
	}
	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Y, &y);
	if (ret < 0) {
		LOG_ERR("IMU gyro y get failed: %d", ret);
		return ret;
	}
	ret = sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Z, &z);
	if (ret < 0) {
		LOG_ERR("IMU gyro z get failed: %d", ret);
		return ret;
	}

	LOG_INF("IMU gyro  x:%.3f y:%.3f z:%.3f rad/s",
		(double)sv_to_float(&x), (double)sv_to_float(&y),
		(double)sv_to_float(&z));

	return 0;
}

/* ================================================================
 * 5. Battery parameter read (NPM1300 charger sensors)
 * ================================================================ */

static int do_battery_read(void)
{
	struct sensor_value value;
	float voltage, current, temp;
	int32_t chg_status;
	int ret;

	ret = sensor_sample_fetch(charger_dev);
	if (ret < 0) {
		LOG_ERR("Battery: sensor fetch failed: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	if (ret < 0) {
		LOG_ERR("Battery: voltage read failed: %d", ret);
		return ret;
	}
	voltage = sv_to_float(&value);

	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_GAUGE_TEMP, &value);
	if (ret < 0) {
		LOG_ERR("Battery: temperature read failed: %d", ret);
		return ret;
	}
	temp = sv_to_float(&value);

	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	if (ret < 0) {
		LOG_ERR("Battery: current read failed: %d", ret);
		return ret;
	}
	current = sv_to_float(&value);

	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
	if (ret < 0) {
		LOG_ERR("Battery: charger status read failed: %d", ret);
		return ret;
	}
	chg_status = value.val1;

	LOG_INF("Battery V:%.3f I:%.3f mA T:%.1f C Status:%s",
		(double)voltage, (double)(current * 1000.0f), (double)temp,
		charge_status_str(chg_status));

	return 0;
}

/* ================================================================
 * 6. BLE non-connectable advertising
 * ================================================================ */

static int init_ble_advertising(void)
{
	int ret;

	ret = bt_enable(NULL);
	if (ret < 0) {
		LOG_ERR("BLE: enable failed: %d", ret);
		return ret;
	}

	ret = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret < 0) {
		LOG_ERR("BLE: adv start failed: %d", ret);
		return ret;
	}

	LOG_INF("BLE: non-connectable advertising started (%s)", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static void feed_watchdog(void)
{
	if (wdt_channel_id >= 0 && wdt_dev) {
		int ret = wdt_feed(wdt_dev, wdt_channel_id);
		if (ret < 0) {
			LOG_ERR("WDT feed failed: %d", ret);
		}
	}
}

static int init_watchdog(void)
{
	int ret;
	struct wdt_timeout_cfg cfg = {
		.window = {
			.min = 0U,
			.max = WDT_TIMEOUT_MS,
		},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};

	if (!wdt_dev || !device_is_ready(wdt_dev)) {
		LOG_WRN("WDT not ready, run without watchdog");
		return -ENODEV;
	}

	wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
	if (wdt_channel_id < 0) {
		LOG_ERR("WDT install timeout failed: %d", wdt_channel_id);
		return wdt_channel_id;
	}

	ret = wdt_setup(wdt_dev, 0);
	if (ret < 0) {
		LOG_ERR("WDT setup failed: %d", ret);
		wdt_channel_id = -1;
		return ret;
	}

	LOG_INF("WDT enabled (%d ms)", WDT_TIMEOUT_MS);
	return 0;
}

static int request_hfxo_and_hold(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF)
	int ret;
	int res = 0;
	struct onoff_manager *clk_mgr;

	if (hfclk_held) {
		return 0;
	}

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("HFCLK manager not available");
		hfclk_last_err = -ENODEV;
		return -ENODEV;
	}

	sys_notify_init_spinwait(&hfclk_hold_cli.notify);

	ret = onoff_request(clk_mgr, &hfclk_hold_cli);
	if (ret < 0) {
		LOG_ERR("HFCLK request failed: %d", ret);
		hfclk_last_err = ret;
		return ret;
	}

	do {
		ret = sys_notify_fetch_result(&hfclk_hold_cli.notify, &res);
		if (!ret && res) {
			LOG_ERR("HFCLK start failed: %d", res);
			hfclk_last_err = res;
			return res;
		}
	} while (ret == -EAGAIN);

	if (ret < 0) {
		LOG_ERR("HFCLK start wait failed: %d", ret);
		hfclk_last_err = ret;
		return ret;
	}

	hfclk_held = true;
	hfclk_last_err = 0;
	LOG_INF("HFCLK: HFXO request granted and held");
	return 0;
#else
	return 0;
#endif
}

static int verify_hfxo_running(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF)
	nrf_clock_hfclk_t hf_src = NRF_CLOCK_HFCLK_LOW_ACCURACY;
	bool hfxo_running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK,
						 &hf_src);

	if (hfxo_running && (hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
		return 0;
	}

	return -EIO;
#else
	return 0;
#endif
}

static void print_clock_status(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF)
	/* Devicetree configuration */
	bool lfxo_ok = DT_NODE_HAS_STATUS(DT_NODELABEL(lfxo), okay);
	bool hfxo_ok = DT_NODE_HAS_STATUS(DT_NODELABEL(hfxo), okay);

	LOG_INF("CLK: LFXO DT: %s, cap %u fF (%u pF)",
		lfxo_ok ? "okay" : "disabled",
		DT_PROP_OR(DT_NODELABEL(lfxo), load_capacitance_femtofarad, 0),
		DT_PROP_OR(DT_NODELABEL(lfxo), load_capacitance_femtofarad, 0) / 1000);
	LOG_INF("CLK: HFXO DT: %s, cap %u fF (%u pF)",
		hfxo_ok ? "okay" : "disabled",
		DT_PROP_OR(DT_NODELABEL(hfxo), load_capacitance_femtofarad, 0),
		DT_PROP_OR(DT_NODELABEL(hfxo), load_capacitance_femtofarad, 0) / 1000);

	/* Kconfig LF clock source */
	LOG_INF("CLK: Kconfig LF src: %s",
		IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL) ? "XTAL (LFXO)" :
		"non-XTAL");

	/* Runtime running status via nrf HAL */
	nrf_clock_hfclk_t hf_src = NRF_CLOCK_HFCLK_LOW_ACCURACY;
	bool hfxo_running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK,
						 &hf_src);
	const char *sys_hf_src;

	nrf_clock_lfclk_t lf_src = (nrf_clock_lfclk_t)0;
	bool lf_running = nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_LFCLK,
					       &lf_src);

	if (hfxo_running && (hf_src == NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
		sys_hf_src = "HFXO 32MHz";
	} else if (hf_src == NRF_CLOCK_HFCLK_LOW_ACCURACY || !hfxo_running) {
		sys_hf_src = "HFINT (internal RC)";
	} else {
		sys_hf_src = "UNKNOWN";
	}

	LOG_INF("CLK: HFXO %s", hfxo_running ? "RUNNING" : "NOT RUNNING");
	LOG_INF("CLK: System HF source: %s", sys_hf_src);
	if (hfclk_held) {
		LOG_INF("CLK: HFXO hold request: ACTIVE");
	} else if (hfclk_last_err) {
		LOG_INF("CLK: HFXO hold request: FAILED (%d)", hfclk_last_err);
	} else {
		LOG_INF("CLK: HFXO hold request: INACTIVE");
	}
	LOG_INF("CLK: LFCLK %s (active src: %s)",
		lf_running ? "RUNNING" : "STOPPED",
		lf_src == NRF_CLOCK_LFCLK_XTAL ? "LFXO 32.768kHz" :
		lf_src == NRF_CLOCK_LFCLK_RC   ? "LFRC" :
		lf_src == NRF_CLOCK_LFCLK_SYNTH ? "SYNTH" : "UNKNOWN");
#else
	LOG_INF("CLK: CONFIG_CLOCK_CONTROL_NRF not enabled");
#endif
}

typedef int (*demo_step_fn_t)(void);

static int run_step(const char *name, demo_step_fn_t fn)
{
	int ret;
	int64_t t0 = k_uptime_get();

	ret = fn();
	if (ret < 0) {
		LOG_ERR("%s failed: %d (cost %lld ms)", name, ret,
			(long long)(k_uptime_get() - t0));
	} else {
		LOG_INF("%s OK (cost %lld ms)", name,
			(long long)(k_uptime_get() - t0));
	}

	return ret;
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
	int ret;

	LOG_INF("=== All-in-One Demo for XIAO nRF54LM20A ===");

	/* Check critical devices */
	if (!device_is_ready(dmic_dev)) {
		LOG_ERR("DMIC device not ready");
		return -ENODEV;
	}
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -ENODEV;
	}
	if (!device_is_ready(charger_dev)) {
		LOG_ERR("Charger device not ready");
		return -ENODEV;
	}

	/* Power up */
	ret = init_power();
	if (ret < 0) {
		return ret;
	}

	/* Initialize RGB LED */
	init_led();

	/* Configure DMIC channel map */
	dmic_config.channel.req_chan_map_lo =
		dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);

	/* Enable watchdog to auto-recover from hard hang */
	(void)init_watchdog();

	ret = request_hfxo_and_hold();
	if (ret < 0) {
		LOG_ERR("HFXO required but request failed (%d)", ret);
		return ret;
	}

	ret = verify_hfxo_running();
	if (ret < 0) {
		LOG_ERR("HFXO required but not running");
		return ret;
	}
	print_clock_status();

	/* Initialize IMU (deferred init) */
	ret = init_imu();
	if (ret < 0) {
		LOG_WRN("IMU init failed (%d), will skip IMU reads", ret);
	}

	/* Start BLE advertising (runs in background) */
	ret = init_ble_advertising();
	if (ret < 0) {
		LOG_WRN("BLE init failed (%d), continuing without BLE", ret);
	}

	LOG_INF("All subsystems initialized, entering main loop");

	while (1) {
		int cycle_failures = 0;
		int64_t cycle_start = k_uptime_get();

		LOG_INF("=== Cycle %u Start (uptime %lld ms) ===",
			cycle_count++, (long long)cycle_start);
		led_toggle(); /* Green LED toggle */
		feed_watchdog();

		ret = verify_hfxo_running();
		if (ret < 0) {
			LOG_ERR("HFXO not running, system fell back to HFINT");
			cycle_failures++;
		}
		print_clock_status();

		if (run_step("DMIC", do_mic_recording) < 0) {
			cycle_failures++;
		}
		feed_watchdog();

		if (run_step("FLASH", do_flash_test) < 0) {
			cycle_failures++;
		}
		feed_watchdog();

		if (run_step("IMU", do_imu_read) < 0) {
			cycle_failures++;
		}
		feed_watchdog();

		if (run_step("BATTERY", do_battery_read) < 0) {
			cycle_failures++;
		}
		feed_watchdog();

		LOG_INF("BLE: advertising running");
		LOG_INF("=== Cycle End (cost %lld ms, failures %d) ===",
			(long long)(k_uptime_get() - cycle_start), cycle_failures);

		if (cycle_failures == 0) {
			consecutive_cycle_failures = 0;
		} else {
			consecutive_cycle_failures++;
			LOG_WRN("Consecutive failed cycles: %u",
				consecutive_cycle_failures);
		}



		feed_watchdog();

		k_sleep(K_SECONDS(2));
	}

	return 0;
}
