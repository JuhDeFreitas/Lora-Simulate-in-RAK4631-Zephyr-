#ifndef LORA_H
#define LORA_H

#include "icc_types.h"

os_int32_t lora_init(void);
os_int32_t app_lora_send(const os_uint8_t *data, os_uint8_t length);

#endif /* LORA_H */