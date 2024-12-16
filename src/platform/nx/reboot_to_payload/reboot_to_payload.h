#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <switch.h>

bool validate_payload_from_path(const char* path, bool check_hekate);
bool validate_payload_from_file(FsFile* file, bool check_hekate);

bool is_r2p_supported(void);
bool reboot_to_payload(u8* iwram_buf, u32 size);

#ifdef __cplusplus
}
#endif
