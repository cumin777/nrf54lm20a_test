#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(zephyr_imu, LOG_LEVEL_INF);

#define IMU_NODE DT_ALIAS(imu0)

#if DT_NODE_HAS_PROP(IMU_NODE, irq_gpios)
static const struct gpio_dt_spec imu_int_gpio =
	GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);
#define IMU_INT_PORT_NUM DT_PROP(DT_GPIO_CTLR(IMU_NODE, irq_gpios), port)
#define IMU_INT_AVAILABLE 1
#else
#define IMU_INT_AVAILABLE 0
#endif

struct imu_sample_cache {
	struct sensor_value accel_x;
	struct sensor_value accel_y;
	struct sensor_value accel_z;
	struct sensor_value gyro_x;
	struct sensor_value gyro_y;
	struct sensor_value gyro_z;
};

static struct imu_sample_cache latest_sample;
static K_MUTEX_DEFINE(sample_lock);
static atomic_t trigger_count;
static atomic_t sample_ready;

#if defined(DT_N_NODELABEL_power_en)
static const struct device *const power_en_dev = DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif

#if defined(DT_N_NODELABEL_imu_vdd)
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif

static int enable_imu_power(void)
{
#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	int ret;
#endif

#if defined(DT_N_NODELABEL_power_en)
	if (!device_is_ready(power_en_dev)) {
		LOG_ERR("power_en regulator is not ready");
		return -ENODEV;
	}

	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable power_en: %d", ret);
		return ret;
	}
#endif

#if defined(DT_N_NODELABEL_imu_vdd)
	if (!device_is_ready(imu_vdd_dev)) {
		LOG_ERR("imu_vdd regulator is not ready");
		return -ENODEV;
	}

	ret = regulator_enable(imu_vdd_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable imu_vdd: %d", ret);
		return ret;
	}
#endif

#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	k_sleep(K_MSEC(20));
#endif

	return 0;
}

static void log_sensor_triplet(const char *label, const char *unit,
			       const struct sensor_value *x,
			       const struct sensor_value *y,
			       const struct sensor_value *z)
{
	int64_t values[3] = {
		sensor_value_to_micro(x),
		sensor_value_to_micro(y),
		sensor_value_to_micro(z),
	};
	char signs[3];
	long long wholes[3];
	long long fracs[3];
	int i;

	for (i = 0; i < 3; i++) {
		if (values[i] < 0) {
			signs[i] = '-';
			values[i] = -values[i];
		} else {
			signs[i] = '+';
		}

		wholes[i] = values[i] / 1000000LL;
		fracs[i] = values[i] % 1000000LL;
	}

	LOG_INF("%s x:%c%lld.%06lld %s y:%c%lld.%06lld %s z:%c%lld.%06lld %s",
		label,
		signs[0], wholes[0], fracs[0], unit,
		signs[1], wholes[1], fracs[1], unit,
		signs[2], wholes[2], fracs[2], unit);
}

static int fetch_latest_sample(const struct device *dev)
{
	struct imu_sample_cache sample;
	int ret;

	ret = sensor_sample_fetch(dev);
	if (ret < 0) {
		LOG_ERR("sensor_sample_fetch failed: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &sample.accel_x);
	if (ret < 0) {
		LOG_ERR("Failed to read accel x: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &sample.accel_y);
	if (ret < 0) {
		LOG_ERR("Failed to read accel y: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &sample.accel_z);
	if (ret < 0) {
		LOG_ERR("Failed to read accel z: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &sample.gyro_x);
	if (ret < 0) {
		LOG_ERR("Failed to read gyro x: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &sample.gyro_y);
	if (ret < 0) {
		LOG_ERR("Failed to read gyro y: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &sample.gyro_z);
	if (ret < 0) {
		LOG_ERR("Failed to read gyro z: %d", ret);
		return ret;
	}

	k_mutex_lock(&sample_lock, K_FOREVER);
	latest_sample = sample;
	k_mutex_unlock(&sample_lock);
	atomic_set(&sample_ready, 1);

	return 0;
}

static int set_sampling_freq(const struct device *dev)
{
	int ret = 0;
	struct sensor_value odr_attr;

	/* Raise ODR a bit so INT activity is obvious on the serial console. */
	odr_attr.val1 = 26;
	odr_attr.val2 = 0;

	ret = sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (ret != 0) {
		LOG_ERR("Cannot set sampling frequency for accelerometer.");
		return ret;
	}

	ret = sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
			SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (ret != 0) {
		LOG_ERR("Cannot set sampling frequency for gyro.");
		return ret;
	}

	return 0;
}

static void log_imu_status(void)
{
	static int64_t last_report_ms;
	static int last_trigger_total;
	struct imu_sample_cache sample;
	int64_t now_ms = k_uptime_get();
	int64_t elapsed_ms;
	int trigger_total = atomic_get(&trigger_count);
	int trigger_delta = trigger_total - last_trigger_total;
	int trigger_rate_hz_x10;

	if (last_report_ms == 0) {
		last_report_ms = now_ms;
	}

	elapsed_ms = now_ms - last_report_ms;
	last_trigger_total = trigger_total;
	last_report_ms = now_ms;
	trigger_rate_hz_x10 = elapsed_ms > 0 ?
		(int)((trigger_delta * 10000LL) / elapsed_ms) : 0;

#if IMU_INT_AVAILABLE
	LOG_INF("INT demo: main loop slept %lld ms, IMU INT P%d.%02d fired %d callbacks, rate %d.%d Hz, level now:%d",
		(long long)elapsed_ms, IMU_INT_PORT_NUM, imu_int_gpio.pin,
		trigger_delta, trigger_rate_hz_x10 / 10, trigger_rate_hz_x10 % 10,
		gpio_pin_get_dt(&imu_int_gpio));
#else
	LOG_INF("INT demo: main loop slept %lld ms, sensor callback fired %d times, rate %d.%d Hz",
		(long long)elapsed_ms, trigger_delta,
		trigger_rate_hz_x10 / 10, trigger_rate_hz_x10 % 10);
#endif

	LOG_INF("INT totals: sensor_triggers=%d", trigger_total);

	if (!atomic_get(&sample_ready)) {
		LOG_INF("Waiting for first IMU sample...");
		return;
	}

	k_mutex_lock(&sample_lock, K_FOREVER);
	sample = latest_sample;
	k_mutex_unlock(&sample_lock);

	log_sensor_triplet("accel", "m/s^2",
			   &sample.accel_x, &sample.accel_y, &sample.accel_z);
	log_sensor_triplet("gyro ", "rad/s",
			   &sample.gyro_x, &sample.gyro_y, &sample.gyro_z);
}

#ifdef CONFIG_LSM6DSL_TRIGGER
static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig)
{
	ARG_UNUSED(trig);
	int total;

	total = atomic_inc(&trigger_count) + 1;
	(void)fetch_latest_sample(dev);

	if ((total % 52) == 0) {
		LOG_INF("INT callback heartbeat: %d sensor data-ready interrupts received", total);
	}
}

static int test_trigger_mode(const struct device *dev)
{
	struct sensor_trigger trig;
	int ret;

	ret = set_sampling_freq(dev);
	if (ret != 0) {
		return ret;
	}

	trig.type = SENSOR_TRIG_DATA_READY;
	trig.chan = SENSOR_CHAN_ACCEL_XYZ;

	ret = sensor_trigger_set(dev, &trig, trigger_handler);
	if (ret != 0) {
		LOG_ERR("Could not set sensor type and channel: %d", ret);
		return ret;
	}

	return 0;
}

#else
static int test_polling_mode(const struct device *dev)
{
	return set_sampling_freq(dev);
}
#endif

int main(void)
{
	int ret;

	ret = enable_imu_power();
	if (ret < 0) {
		LOG_ERR("Failed to enable IMU power: %d", ret);
		return 0;
	}

	const struct device *const dev = DEVICE_DT_GET(IMU_NODE);

	if (!device_is_ready(dev)) {
		ret = device_init(dev);
		if (ret < 0 && ret != -EALREADY) {
			LOG_ERR("Failed to initialize %s: %d", dev->name, ret);
			return 0;
		}
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("%s: device not ready.", dev->name);
		return 0;
	}

#ifdef CONFIG_LSM6DSL_TRIGGER
	LOG_INF("Testing LSM6DSL sensor in trigger mode via INT pin P%d.%02d.",
		IMU_INT_PORT_NUM, imu_int_gpio.pin);
	LOG_INF("The main loop reports every 2 seconds, but INT keeps running in the background.");
	ret = test_trigger_mode(dev);
#else
	LOG_INF("Testing LSM6DSL sensor in polling mode.");
	ret = test_polling_mode(dev);
#endif

	if (ret < 0) {
		return 0;
	}

	while (1) {
#ifndef CONFIG_LSM6DSL_TRIGGER
		(void)fetch_latest_sample(dev);
#endif
		log_imu_status();
		k_sleep(K_MSEC(2000));
	}

	return 0;
}
