#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

#include "icc_error.h"
#include "icc_log.h"
#include "icc_types.h"
#include "lora/lora.h"
#include "lorawan/lorawan.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


#define PUB_MSG_INTERVAL 10

os_int32_t up_lorawan(){
	/* LORAWAN CONFIG */

	os_uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00};
	os_uint8_t counter = 0;
	
	if (lorawan_init() != ICC_OK) {
		return 0;
	}

	while (1) {
		payload[4] = counter;

		if (lorawan_send_payload(payload, sizeof(payload) ) != ICC_OK) {
			ICC_LOG_ERROR( ICC_ERR_LORA_SEND, "LoRaWAN payload send failed.");
		}
		counter += 1;
		k_sleep(K_SECONDS(PUB_MSG_INTERVAL));
	}

	return ICC_OK;
	
}

os_int32_t up_lora(){
	/* LORA CONFIG */

	os_uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00};
	os_uint8_t counter = 0;
	
	if (lora_init() != ICC_OK) {
		ICC_LOG_ERROR(ICC_ERR_LORA_INIT, "Failed to initialize LoRa.");
		return ICC_ERROR;
	}

	while (1) {
		payload[4] = counter;

		if (app_lora_send(payload, sizeof(payload)) != ICC_OK) {
			ICC_LOG_ERROR(ICC_ERR_LORA_SEND,
				      "Error sending payload by LoRa.");
		} else {
			ICC_LOG_INFO("LoRa payload sent: %d", payload[4]);
		}

		counter += 1;
		k_sleep(K_SECONDS(5));
	}

	return ICC_OK;
}

os_int32_t main(void)
{
	ICC_LOG_INFO("RAK4631 application started (Zephyr %s)", KERNEL_VERSION_STRING);
	
	//up_lora();
	up_lorawan();

	return 0;
	
}