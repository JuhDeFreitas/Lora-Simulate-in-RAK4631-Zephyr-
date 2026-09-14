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

#define LORA_FREQ_HZ 902700000

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

	int ret = lora_send(lora_dev, (uint8_t *)data, length);

	if (ret < 0) {
		ICC_LOG_ERROR(
			ICC_ERR_LORA_SEND,
			"LoRa send failed: %d",
			ret
		);

		return ICC_ERROR;
	}

	return ICC_OK;
}