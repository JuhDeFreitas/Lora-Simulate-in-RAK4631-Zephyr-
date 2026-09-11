/**
 * @file main.c
 * @brief RAK4631 (nRF52840) Zephyr application entry point.
 *
 * @copyright
 * (c) 2026, Inatel Competence Center
 * All rights are reserved. Reproduction in whole or part is prohibited without
 * the written consent of the copyright owner.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/device.h>

#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

#include "icc_error.h"
#include "icc_log.h"
#include "icc_types.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LORA_NODE DT_ALIAS(lora0)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LORA_NODE), "Lora alias missing in device tree.");

static const struct device *const lora_dev = DEVICE_DT_GET(LORA_NODE);

//#define LORA_FREQ_HZ 915200000
#define LORA_FREQ_HZ 902700000   /* 902.7 MHz, canal 2 da sub-band 1 */


os_int32_t main(void)
{
	ICC_LOG_INFO("RAK4631 application started (Zephyr %s)", KERNEL_VERSION_STRING);

	struct lora_modem_config cfg = {
		.frequency       = LORA_FREQ_HZ,
		.bandwidth       = BW_125_KHZ,
		.datarate        = SF_10,
		.coding_rate     = CR_4_5,
		.preamble_len    = 8,
		.tx_power        = 14,      /* dBm; SX1262 vai até ~22 */
		.iq_inverted     = false,
		.public_network  = false,
		.tx              = true,    /* configura o rádio para transmitir */
	};

	if(!device_is_ready(lora_dev)){
		ICC_LOG_ERROR(ICC_ERR_LORA_INIT, "LoRa device %s is not ready.", lora_dev->name);
		return ICC_ERROR;
	}

	if(lora_config(lora_dev,&cfg) < 0){
		ICC_LOG_ERROR(ICC_ERR_LORA_INIT, "Lora config Fail.");
		return ICC_ERROR;
	}

	os_uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};

	while (1) {
		os_int8_t ret = lora_send(lora_dev, payload, sizeof(payload));

		if (ret < 0){
			ICC_LOG_ERROR(ICC_ERR_LORA_SEND, "Error to send payload by LoRa. %d", ret);
		}else {
			ICC_LOG_INFO("LoRa payload sent.");
		}

		k_sleep(K_SECONDS(5));
	}

	return ICC_OK;
}
