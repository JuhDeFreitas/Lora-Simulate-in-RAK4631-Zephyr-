#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/lorawan/lorawan.h>

#include "icc_error.h"
#include "icc_log.h"
#include "icc_types.h"
#include "lorawan.h"

#define LORA_NODE DT_ALIAS(lora0)

BUILD_ASSERT(
	DT_NODE_HAS_STATUS_OKAY(LORA_NODE),
	"LoRa alias missing in device tree."
);

static const struct device *const lora_dev = DEVICE_DT_GET(LORA_NODE);

/*
 * LoRaWAN credentials.
 *
 * These values must be replaced with the credentials
 * configured in the LoRaWAN Network Server.
 */
#define LORAWAN_DEV_EUI \
	{ 0xDD, 0xEE, 0xAA, 0xDD, \
	  0xBB, 0xEE, 0xEE, 0xFF }

#define LORAWAN_JOIN_EUI \
	{ 0x00, 0x00, 0x00, 0x00, \
	  0x00, 0x00, 0x00, 0x00 }

#define LORAWAN_APP_KEY \
	{ 0x2B, 0x7E, 0x15, 0x16, \
	  0x28, 0xAE, 0xD2, 0xA6, \
	  0xAB, 0xF7, 0x15, 0x88, \
	  0x09, 0xCF, 0x4F, 0x3C }

#define LORAWAN_PORT 2

/* Delay between OTAA join retries while the network hasn't accepted us yet. */
#define LORAWAN_JOIN_RETRY_DELAY_S 10

/*
 * Callback executed when a LoRaWAN downlink is received.
 */
static void lorawan_downlink_callback(
	uint8_t port,
	uint8_t flags,
	int16_t rssi,
	int8_t snr,
	uint8_t len,
	const uint8_t *data)
{
	ICC_LOG_INFO(
		"LoRaWAN downlink received: port=%d, RSSI=%d dBm, SNR=%d dB, length=%d",
		port,
		rssi,
		snr,
		len
	);

	if (flags & LORAWAN_TIME_UPDATED) {
		ICC_LOG_INFO("LoRaWAN network time updated.");
	}

	if (flags & LORAWAN_DATA_PENDING) {
		ICC_LOG_INFO("LoRaWAN network has more data pending.");
	}

	if (data != NULL && len > 0) {
		ICC_LOG_INFO("LoRaWAN payload:");

		for (uint8_t i = 0; i < len; i++) {
			ICC_LOG_INFO(
				"data[%d] = 0x%02X",
				i,
				data[i]
			);
		}
	}
}

/*
 * Callback executed when the LoRaWAN data rate changes.
 */
static void lorawan_datarate_changed(
	enum lorawan_datarate datarate)
{
	uint8_t max_next_payload;
	uint8_t max_payload;

	lorawan_get_payload_sizes(
		&max_next_payload,
		&max_payload
	);

	ICC_LOG_INFO(
		"LoRaWAN datarate changed: DR_%d, max payload=%d",
		datarate,
		max_payload
	);
}

/*
 * Initialize the LoRaWAN stack and join the network using OTAA.
 */
os_int32_t lorawan_init(void)
{
	struct lorawan_join_config join_cfg;

	uint8_t dev_eui[] = LORAWAN_DEV_EUI;
	uint8_t join_eui[] = LORAWAN_JOIN_EUI;
	uint8_t app_key[] = LORAWAN_APP_KEY;

	int ret;

	/*
	 * Check if the LoRa device is ready.
	 */
	if (!device_is_ready(lora_dev)) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRa device %s is not ready.",
			lora_dev->name
		);

		return ICC_ERROR;
	}

	/*
	 * Start the LoRaWAN stack.
	 */
	ret = lorawan_start();

	if (ret < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRaWAN start failed: %d",
			ret
		);

		return ICC_ERROR;
	}

	/*
	 * Register the downlink callback.
	 *
	 * LW_RECV_PORT_ANY means that the callback will receive
	 * downlinks arriving on any application port.
	 */
	static struct lorawan_downlink_cb downlink_cb = {
		.port = LW_RECV_PORT_ANY,
		.cb = lorawan_downlink_callback,
	};

	lorawan_register_downlink_callback(&downlink_cb);

	/*
	 * Register the datarate callback.
	 */
	lorawan_register_dr_changed_callback(
		lorawan_datarate_changed
	);

	/*
	 * Configure OTAA.
	 */
	join_cfg.mode = LORAWAN_ACT_OTAA;

	join_cfg.dev_eui = dev_eui;

	join_cfg.otaa.join_eui = join_eui;
	join_cfg.otaa.app_key = app_key;
	join_cfg.otaa.nwk_key = app_key;

	/*
	 * Keep retrying the join until the network accepts us. Each attempt
	 * uses a new DevNonce, since compliant network servers reject a
	 * repeated nonce for the same DevEUI.
	 */
	for (uint16_t attempt = 1u; ; attempt++) {
		join_cfg.otaa.dev_nonce = attempt - 1u;

		ICC_LOG_INFO(
			"Joining LoRaWAN network using OTAA (attempt %u)...",
			attempt
		);

		ret = lorawan_join(&join_cfg);

		if (ret == 0) {
			break;
		}

		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRaWAN join failed: %d, retrying in %d s",
			ret,
			LORAWAN_JOIN_RETRY_DELAY_S
		);

		k_sleep(K_SECONDS(LORAWAN_JOIN_RETRY_DELAY_S));
	}

	ICC_LOG_INFO(
		"LoRaWAN initialized and joined successfully."
	);

	return ICC_OK;
}

/*
 * Send a LoRaWAN uplink message.
 */
os_int32_t lorawan_send_payload(
	const os_uint8_t *data,
	os_uint8_t length)
{
	if (data == NULL || length == 0) {
		return ICC_ERROR;
	}

	int ret;

	ret = lorawan_send(
		LORAWAN_PORT,
		(uint8_t *)data,
		length,
		LORAWAN_MSG_UNCONFIRMED
	);

	if (ret < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_SEND,
			"LoRaWAN send failed: %d",
			ret
		);

		return ICC_ERROR;
	}

	ICC_LOG_INFO(
		"LoRaWAN uplink sent successfully."
	);

	return ICC_OK;
}