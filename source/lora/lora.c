#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/lora.h>

#include "icc_error.h"
#include "icc_log.h"
#include "icc_types.h"
#include "lora.h"

#define LORA_NODE DT_ALIAS(lora0)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LORA_NODE),
	     "LoRa alias missing in device tree.");

static const struct device *const lora_dev = DEVICE_DT_GET(LORA_NODE);

/*
 * Temporarily set to 917.2 MHz (one of the AU915 sub-band 2 channels used
 * by the LoRaWAN side) to test whether packet loss is frequency-dependent
 * rather than a LoRaWAN configuration/protocol issue. Was 902700000.
 */
#define LORA_FREQ_HZ 917200000

/* Number of send attempts before giving up on a payload. */
#define LORA_SEND_RETRY_COUNT 4

/* Delay between send retries. */
#define LORA_SEND_RETRY_DELAY_S 2

/*
 * When enabled, each send hops to the next frequency in LORA_HOP_FREQUENCIES
 * instead of staying on LORA_FREQ_HZ. Used to test whether packet loss is
 * caused by channel hopping itself (matching LoRaWAN's behavior) rather than
 * by LoRaWAN's protocol/downlink activity.
 */
#define LORA_FREQ_HOP_ENABLE true

/* AU915 sub-band 2 channel frequencies, matching the LoRaWAN channel mask. */
static const uint32_t LORA_HOP_FREQUENCIES[] = {
	916800000, 917000000, 917200000, 917400000,
	917600000, 917800000, 918000000, 918200000,
};

static uint8_t lora_hop_index;

static struct lora_modem_config lora_cfg = {
	.frequency = LORA_FREQ_HZ,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_10,
	.coding_rate = CR_4_5,
	.preamble_len = 8,
	.tx_power = 14,
	.iq_inverted = false,
	.public_network = false,
	.tx = true,
};

os_int32_t lora_init(void)
{
	if (!device_is_ready(lora_dev)) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRa device %s is not ready.",
			lora_dev->name
		);

		return ICC_ERROR;
	}

	if (lora_config(lora_dev, &lora_cfg) < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_INIT,
			"LoRa configuration failed."
		);

		return ICC_ERROR;
	}

	ICC_LOG_INFO(
		"LoRa initialized: %d Hz, BW %d, SF %d",
		lora_cfg.frequency,
		lora_cfg.bandwidth,
		lora_cfg.datarate
	);

	return ICC_OK;
}

os_int32_t app_lora_send(const os_uint8_t *data, os_uint8_t length)
{
	if (data == NULL || length == 0) {
		return ICC_ERROR;
	}

	int ret;

	if (LORA_FREQ_HOP_ENABLE) {
		lora_cfg.frequency = LORA_HOP_FREQUENCIES[lora_hop_index];
		lora_hop_index = (lora_hop_index + 1) % ARRAY_SIZE(LORA_HOP_FREQUENCIES);

		if (lora_config(lora_dev, &lora_cfg) < 0) {
			ICC_LOG_ERROR(
				ICC_ERR_LORA_INIT,
				"LoRa hop frequency reconfigure failed."
			);

			return ICC_ERROR;
		}
	}

	/*
	 * Plain point-to-point LoRa has no MAC-layer ACK, so this is a blind
	 * resend on failure, not a confirmed delivery like LoRaWAN's.
	 */
	for (uint8_t attempt = 1u; attempt <= LORA_SEND_RETRY_COUNT; attempt++) {
		ret = lora_send(lora_dev, (uint8_t *)data, length);

		if (ret == 0) {
			return ICC_OK;
		}

		ICC_LOG_ERROR(
			ICC_ERR_LORA_SEND,
			"LoRa send failed (attempt %u/%u): %d",
			attempt,
			LORA_SEND_RETRY_COUNT,
			ret
		);

		if (attempt < LORA_SEND_RETRY_COUNT) {
			k_sleep(K_SECONDS(LORA_SEND_RETRY_DELAY_S));
		}
	}

	return ICC_ERROR;
}