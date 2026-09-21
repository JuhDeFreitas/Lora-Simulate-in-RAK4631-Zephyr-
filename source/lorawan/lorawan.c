#include <stdbool.h>
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
	  0xBB, 0xEE, 0xEE, 0x00}

#define LORAWAN_JOIN_EUI \
	{ 0x00, 0x00, 0x00, 0x00, \
	  0x00, 0x00, 0x00, 0x00 }

#define LORAWAN_APP_KEY \
	{ 0x2B, 0x7E, 0x15, 0x16, \
	  0x28, 0xAE, 0xD2, 0xA6, \
	  0xAB, 0xF7, 0x15, 0x88, \
	  0x09, 0xCF, 0x4F, 0x3C }

/*
 * ABP activation: bypasses OTAA join entirely (no join-accept, no downlink
 * at all during the session), used to test whether packet loss is caused
 * by the gateway's downlink activity. These values must be registered as
 * an ABP device on the Network Server before joining.
 */
#define LORAWAN_ACTIVATION_ABP false

#define LORAWAN_ABP_DEV_ADDR 0x06e90308

#define LORAWAN_ABP_NWK_SKEY \
	{ 0xD0, 0x2D, 0x0B, 0xE1, \
	  0xC1, 0x70, 0x3E, 0x68, \
	  0x62, 0x2C, 0x5C, 0x8D, \
	  0x03, 0xCF, 0x90, 0x42 }

#define LORAWAN_ABP_APP_SKEY \
	{ 0x12, 0x7C, 0xC8, 0x4B, \
	  0x6D, 0x21, 0x55, 0x6B, \
	  0xE7, 0x6B, 0x69, 0x2C, \
	  0xFB, 0x86, 0x43, 0x41 }

#define LORAWAN_PORT 2

/* Delay between OTAA join retries while the network hasn't accepted us yet. */
#define LORAWAN_JOIN_RETRY_DELAY_S 30

/*
 * Adaptive Data Rate: lets the network adjust the device's datarate/TX
 * power to the observed link quality instead of it staying fixed
 * regardless of conditions.
 */
#define LORAWAN_ADR_ENABLE true

/*
 * Confirmed uplinks request a network ACK and are retried at the MAC
 * layer if unacknowledged, at the cost of extra airtime/duty cycle.
 * Unconfirmed uplinks are fire-and-forget.
 */
#define LORAWAN_CONFIRMED_MSG_ENABLE true

/* Number of MAC-layer retransmission attempts for confirmed uplinks. */
#define LORAWAN_CONF_MSG_TRIES 4

/*
 * AU915 channel mask restricting the device to Frequency Sub-Band 2
 * (125 kHz channels 8-15 plus the 500 kHz channel 65), matching the
 * gateway's configured sub-band. Without this, join/uplink attempts are
 * spread across all 72 AU915 channels and the gateway - which only
 * demodulates one sub-band at a time - misses most of them.
 */
#define LORAWAN_CHANNEL_MASK \
	{ 0xFF00, 0x0000, 0x0000, 0x0000, 0x0002, 0x0000 }

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
	 * Must be called after lorawan_start(); calls before start are ignored.
	 */
	lorawan_enable_adr(LORAWAN_ADR_ENABLE);

	/*
	 * Number of MAC-layer retries for confirmed uplinks, so a single lost
	 * uplink or ACK doesn't fail the whole send.
	 */
	ret = lorawan_set_conf_msg_tries(LORAWAN_CONF_MSG_TRIES);

	if (ret < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRaWAN confirmed message tries configuration failed: %d",
			ret
		);

		return ICC_ERROR;
	}

	/*
	 * Restrict the channel plan to the sub-band the gateway is listening
	 * on. Must be called after lorawan_start() and before lorawan_join()
	 * so it takes effect for join channel selection too.
	 */
	uint16_t channel_mask[] = LORAWAN_CHANNEL_MASK;

	ret = lorawan_set_channels_mask(
		channel_mask,
		ARRAY_SIZE(channel_mask)
	);

	if (ret < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRaWAN channel mask configuration failed: %d",
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

	join_cfg.dev_eui = dev_eui;

	if (LORAWAN_ACTIVATION_ABP) {
		/*
		 * ABP: session keys are pre-shared, so there is no OTAA
		 * join-accept and no downlink at all during activation.
		 */
		uint8_t nwk_skey[] = LORAWAN_ABP_NWK_SKEY;
		uint8_t app_skey[] = LORAWAN_ABP_APP_SKEY;

		join_cfg.mode = LORAWAN_ACT_ABP;

		join_cfg.abp.dev_addr = LORAWAN_ABP_DEV_ADDR;
		join_cfg.abp.app_eui = join_eui;
		join_cfg.abp.nwk_skey = nwk_skey;
		join_cfg.abp.app_skey = app_skey;

		ICC_LOG_INFO("Activating LoRaWAN via ABP...");

		ret = lorawan_join(&join_cfg);

		if (ret < 0) {
			ICC_LOG_ERROR(
				ICC_ERR_LORA_INIT,
				"LoRaWAN ABP activation failed: %d",
				ret
			);

			return ICC_ERROR;
		}
	} else {
		join_cfg.mode = LORAWAN_ACT_OTAA;

		join_cfg.otaa.join_eui = join_eui;
		join_cfg.otaa.app_key = app_key;
		join_cfg.otaa.nwk_key = app_key;

		/*
		 * Keep retrying the join until the network accepts us. Each
		 * attempt uses a new DevNonce, since compliant network
		 * servers reject a repeated nonce for the same DevEUI.
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
		LORAWAN_CONFIRMED_MSG_ENABLE ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED
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
		"LoRaWAN uplink sent successfully: %d.", data[4]
	);

	return ICC_OK;
}