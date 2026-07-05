/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>

#define MAX_REC_COUNT           3
#define NDEF_MSG_BUF_SIZE       128
#define HEARTBEAT_INTERVAL_MS   2000

#define NFC_FIELD_LED_NODE      DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(NFC_FIELD_LED_NODE, okay)
#error "Unsupported board: alias led0 is required for NFC field indication"
#endif

/* Text message in English with its language code. */
static const uint8_t en_payload[] = {
	'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'
};
static const uint8_t en_code[] = {'e', 'n'};

/* Text message in Norwegian with its language code. */
static const uint8_t no_payload[] = {
	'H', 'a', 'l', 'l', 'o', ' ', 'V', 'e', 'r', 'd', 'e', 'n', '!'
};
static const uint8_t no_code[] = {'N', 'O'};

/* Text message in Polish with its language code. */
static const uint8_t pl_payload[] = {
	'W', 'i', 't', 'a', 'j', ' ', 0xc5, 0x9a, 'w', 'i', 'e', 'c', 'i',
	'e', '!'
};
static const uint8_t pl_code[] = {'P', 'L'};

/* Buffer used to hold an NFC NDEF message. */
static uint8_t ndef_msg_buf[NDEF_MSG_BUF_SIZE];
static const struct gpio_dt_spec nfc_field_led = GPIO_DT_SPEC_GET(NFC_FIELD_LED_NODE, gpios);
static bool nfc_field_active;

static int nfc_field_led_init(void)
{
	if (!gpio_is_ready_dt(&nfc_field_led)) {
		printk("NFC LED GPIO is not ready\n");
		return -ENODEV;
	}

	return gpio_pin_configure_dt(&nfc_field_led, GPIO_OUTPUT_INACTIVE);
}

static void nfc_field_led_set(bool on)
{
	int err = gpio_pin_set_dt(&nfc_field_led, on ? 1 : 0);

	if (err < 0) {
		printk("Cannot set NFC LED state: %d\n", err);
	}
}

static void nfc_callback(void *context,
			 nfc_t2t_event_t event,
			 const uint8_t *data,
			 size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	switch (event) {
	case NFC_T2T_EVENT_FIELD_ON:
		nfc_field_active = true;
		printk("NFC field on\n");
		nfc_field_led_set(true);
		break;
	case NFC_T2T_EVENT_FIELD_OFF:
		nfc_field_active = false;
		printk("NFC field off\n");
		nfc_field_led_set(false);
		break;
	default:
		break;
	}
}

/**
 * @brief Function for encoding the NDEF text message.
 */
static int welcome_msg_encode(uint8_t *buffer, uint32_t *len)
{
	int err;

	/* Create NFC NDEF text record description in English */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_en_text_rec,
				      UTF_8,
				      en_code,
				      sizeof(en_code),
				      en_payload,
				      sizeof(en_payload));

	/* Create NFC NDEF text record description in Norwegian */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_no_text_rec,
				      UTF_8,
				      no_code,
				      sizeof(no_code),
				      no_payload,
				      sizeof(no_payload));

	/* Create NFC NDEF text record description in Polish */
	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_pl_text_rec,
				      UTF_8,
				      pl_code,
				      sizeof(pl_code),
				      pl_payload,
				      sizeof(pl_payload));

	/* Create NFC NDEF message description, capacity - MAX_REC_COUNT
	 * records
	 */
	NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_COUNT);

	/* Add text records to NDEF text message */
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec));
	if (err < 0) {
		printk("Cannot add first record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_no_text_rec));
	if (err < 0) {
		printk("Cannot add second record!\n");
		return err;
	}
	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				   &NFC_NDEF_TEXT_RECORD_DESC(nfc_pl_text_rec));
	if (err < 0) {
		printk("Cannot add third record!\n");
		return err;
	}

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), buffer, len);
	if (err < 0) {
		printk("Cannot encode message!\n");
	}

	return err;
}

int main(void)
{
	uint32_t len = sizeof(ndef_msg_buf);
	uint32_t heartbeat_count = 0U;

	printk("Starting NFC Text Record sample\n");

	if (nfc_field_led_init() < 0) {
		printk("Cannot init NFC field LED!\n");
		goto fail;
	}

	/* Set up NFC */
	if (nfc_t2t_setup(nfc_callback, NULL) < 0) {
		printk("Cannot setup NFC T2T library!\n");
		goto fail;
	}

	/* Encode welcome message */
	if (welcome_msg_encode(ndef_msg_buf, &len) < 0) {
		printk("Cannot encode message!\n");
		goto fail;
	}

	/* Set created message as the NFC payload */
	if (nfc_t2t_payload_set(ndef_msg_buf, len) < 0) {
		printk("Cannot set payload!\n");
		goto fail;
	}

	/* Start sensing NFC field */
	if (nfc_t2t_emulation_start() < 0) {
		printk("Cannot start emulation!\n");
		goto fail;
	}
	printk("NFC configuration done\n");

	while (1) {
		printk("NFC heartbeat %u: field=%s, uptime=%lld ms\n",
		       heartbeat_count++,
		       nfc_field_active ? "on" : "off",
		       k_uptime_get());
		k_msleep(HEARTBEAT_INTERVAL_MS);
	}

fail:
#if CONFIG_REBOOT
	sys_reboot(SYS_REBOOT_COLD);
#endif /* CONFIG_REBOOT */

	return -EIO;
}
