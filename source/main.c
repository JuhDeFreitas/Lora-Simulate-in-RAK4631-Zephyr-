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

#include "icc_error.h"
#include "icc_log.h"
#include "icc_types.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD K_SECONDS(5)

os_int32_t main(void)
{
	ICC_LOG_INFO("RAK4631 application started (Zephyr %s)", KERNEL_VERSION_STRING);

	os_uint32_t beat = 0U;

	while (1) {
		ICC_LOG_DEBUG("heartbeat %u", beat++);
		k_sleep(HEARTBEAT_PERIOD);
	}

	return ICC_OK;
}
