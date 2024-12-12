#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define IRAM_PAYLOAD_MAX_SIZE 0x24000

#include <stdbool.h>
#include <switch.h>

bool is_r2p_supported(void);
bool reboot_to_payload(u8* iwram_buf, u32 size);

#ifdef __cplusplus
}
#endif
