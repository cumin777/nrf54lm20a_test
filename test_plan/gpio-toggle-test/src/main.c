#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_PORTS 3
#define MAX_PINS  32
#define MIN_PULSE_MS 10
#define CMD_BUF_SIZE 128

static const struct device *gpio_devs[MAX_PORTS];
static const struct device *uart_dev;

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos;

static void send_ok(void)
{
	printk("OK\r\n");
}

static void send_error(const char *msg)
{
	printk("ERROR: %s\r\n", msg);
}

static void str_to_upper(char *s)
{
	for (; *s; s++) {
		*s = (char)toupper((unsigned char)*s);
	}
}

static int read_line(void)
{
	while (1) {
		uint8_t c;

		if (uart_poll_in(uart_dev, &c) != 0) {
			k_msleep(1);
			continue;
		}

		/* End of line */
		if (c == '\r' || c == '\n') {
			if (cmd_pos > 0) {
				cmd_buf[cmd_pos] = '\0';
				printk("\r\n");
				cmd_pos = 0;
				return 0;
			}
			continue;
		}

		/* Backspace */
		if (c == 0x08 || c == 0x7F) {
			if (cmd_pos > 0) {
				cmd_pos--;
				printk("\b \b");
			}
			continue;
		}

		/* Buffer overflow: reset */
		if (cmd_pos >= CMD_BUF_SIZE - 1) {
			cmd_pos = 0;
			send_error("command too long");
			continue;
		}

		/* Echo and store */
		cmd_buf[cmd_pos++] = (char)c;
		uart_poll_out(uart_dev, c);
	}
}

static void cmd_help(void)
{
	printk("\r\n");
	printk("=== GPIO Toggle Test AT Commands ===\r\n");
	printk("AT                             - Test connection\r\n");
	printk("AT+HELP                        - Show this help\r\n");
	printk("AT+CFG=<port>,<pin>,<mode>     - Configure pin (O=output, I=input)\r\n");
	printk("AT+SET=<port>,<pin>,<value>    - Set output (0=low, 1=high)\r\n");
	printk("AT+TOGGLE=<port>,<pin>         - Toggle pin once\r\n");
	printk("AT+PULSE=<port>,<pin>,<ms>,<n> - Pulse toggle n times at interval ms\r\n");
	printk("AT+READ=<port>,<pin>           - Read pin value\r\n");
	printk("\r\n");
	printk("Port: 0=GPIO0, 1=GPIO1, 2=GPIO2 | Pin: 0~31\r\n");
	printk("Example:\r\n");
	printk("  AT+CFG=1,24,O       -> Config GPIO1 P24 as output\r\n");
	printk("  AT+SET=1,24,1       -> Set GPIO1 P24 high\r\n");
	printk("  AT+TOGGLE=1,24      -> Toggle GPIO1 P24\r\n");
	printk("  AT+PULSE=1,24,100,5 -> Pulse 5 times, 100ms interval\r\n");
	printk("  AT+READ=1,24        -> Read GPIO1 P24\r\n");
	printk("\r\n");
}

static void cmd_cfg(char *args)
{
	int port, pin;
	char mode;

	if (sscanf(args, "%d,%d,%c", &port, &pin, &mode) != 3) {
		send_error("format: AT+CFG=<port>,<pin>,<O|I>");
		return;
	}

	if (port < 0 || port >= MAX_PORTS || !gpio_devs[port]) {
		send_error("invalid port");
		return;
	}
	if (pin < 0 || pin >= MAX_PINS) {
		send_error("invalid pin (0~31)");
		return;
	}

	int ret;
	if (mode == 'O') {
		ret = gpio_pin_configure(gpio_devs[port], pin, GPIO_OUTPUT_LOW);
	} else if (mode == 'I') {
		ret = gpio_pin_configure(gpio_devs[port], pin, GPIO_INPUT);
	} else {
		send_error("mode must be O or I");
		return;
	}

	if (ret == 0) {
		send_ok();
	} else {
		printk("ERROR: configure failed (%d)\r\n", ret);
	}
}

static void cmd_set(char *args)
{
	int port, pin, val;

	if (sscanf(args, "%d,%d,%d", &port, &pin, &val) != 3) {
		send_error("format: AT+SET=<port>,<pin>,<0|1>");
		return;
	}

	if (port < 0 || port >= MAX_PORTS || !gpio_devs[port]) {
		send_error("invalid port");
		return;
	}
	if (pin < 0 || pin >= MAX_PINS) {
		send_error("invalid pin (0~31)");
		return;
	}

	int ret = gpio_pin_set(gpio_devs[port], pin, val);
	if (ret == 0) {
		send_ok();
	} else {
		printk("ERROR: set failed (%d)\r\n", ret);
	}
}

static void cmd_toggle(char *args)
{
	int port, pin;

	if (sscanf(args, "%d,%d", &port, &pin) != 2) {
		send_error("format: AT+TOGGLE=<port>,<pin>");
		return;
	}

	if (port < 0 || port >= MAX_PORTS || !gpio_devs[port]) {
		send_error("invalid port");
		return;
	}
	if (pin < 0 || pin >= MAX_PINS) {
		send_error("invalid pin (0~31)");
		return;
	}

	int ret = gpio_pin_toggle(gpio_devs[port], pin);
	if (ret == 0) {
		send_ok();
	} else {
		printk("ERROR: toggle failed (%d)\r\n", ret);
	}
}

static void cmd_pulse(char *args)
{
	int port, pin, interval_ms, count;

	if (sscanf(args, "%d,%d,%d,%d", &port, &pin, &interval_ms, &count) != 4) {
		send_error("format: AT+PULSE=<port>,<pin>,<ms>,<count>");
		return;
	}

	if (port < 0 || port >= MAX_PORTS || !gpio_devs[port]) {
		send_error("invalid port");
		return;
	}
	if (pin < 0 || pin >= MAX_PINS) {
		send_error("invalid pin (0~31)");
		return;
	}
	if (interval_ms < MIN_PULSE_MS) {
		interval_ms = MIN_PULSE_MS;
	}
	if (count < 1) {
		count = 1;
	}
	if (count > 10000) {
		count = 10000;
	}

	for (int i = 0; i < count; i++) {
		gpio_pin_toggle(gpio_devs[port], pin);
		k_msleep(interval_ms);
		gpio_pin_toggle(gpio_devs[port], pin);
		k_msleep(interval_ms);
	}

	printk("+PULSE: done (%d pulses, %d ms interval)\r\n", count, interval_ms);
}

static void cmd_read(char *args)
{
	int port, pin;

	if (sscanf(args, "%d,%d", &port, &pin) != 2) {
		send_error("format: AT+READ=<port>,<pin>");
		return;
	}

	if (port < 0 || port >= MAX_PORTS || !gpio_devs[port]) {
		send_error("invalid port");
		return;
	}
	if (pin < 0 || pin >= MAX_PINS) {
		send_error("invalid pin (0~31)");
		return;
	}

	int val = gpio_pin_get(gpio_devs[port], pin);
	if (val >= 0) {
		printk("+READ: %d\r\n", val);
	} else {
		printk("ERROR: read failed (%d)\r\n", val);
	}
}

static void process_command(char *cmd)
{
	while (*cmd && (*cmd == ' ' || *cmd == '\t'))
		cmd++;
	if (*cmd == '\0')
		return;

	char buf[CMD_BUF_SIZE];

	strncpy(buf, cmd, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	str_to_upper(buf);

	if (strcmp(buf, "AT") == 0) {
		send_ok();
	} else if (strcmp(buf, "AT+HELP") == 0) {
		cmd_help();
	} else if (strncmp(buf, "AT+CFG=", 7) == 0) {
		cmd_cfg(buf + 7);
	} else if (strncmp(buf, "AT+SET=", 7) == 0) {
		cmd_set(buf + 7);
	} else if (strncmp(buf, "AT+TOGGLE=", 10) == 0) {
		cmd_toggle(buf + 10);
	} else if (strncmp(buf, "AT+PULSE=", 9) == 0) {
		cmd_pulse(buf + 9);
	} else if (strncmp(buf, "AT+READ=", 8) == 0) {
		cmd_read(buf + 8);
	} else {
		send_error("unknown command, type AT+HELP");
	}
}

int main(void)
{
	gpio_devs[0] = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	gpio_devs[1] = DEVICE_DT_GET(DT_NODELABEL(gpio1));
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio2), okay)
	gpio_devs[2] = DEVICE_DT_GET(DT_NODELABEL(gpio2));
#endif

	uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart20));
	if (!device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	for (int i = 0; i < MAX_PORTS; i++) {
		if (gpio_devs[i] && device_is_ready(gpio_devs[i])) {
			printk("GPIO%d: ready\r\n", i);
		} else {
			gpio_devs[i] = NULL;
		}
	}

	printk("\r\n========================================\r\n");
	printk("  GPIO Toggle Test - AT Command Interface\r\n");
	printk("  UART: 115200 8N1\r\n");
	printk("  Type AT+HELP for commands\r\n");
	printk("========================================\r\n\r\n");

	while (1) {
		if (read_line() == 0) {
			process_command(cmd_buf);
		}
	}

	return 0;
}
