#ifndef LORAWAN_H
#define LORAWAN_H

#include "icc_types.h"
#include <sys/types.h>


os_int32_t lorawan_init(void);

os_int32_t lorawan_send_payload(const os_uint8_t *data, os_uint8_t length);

#endif