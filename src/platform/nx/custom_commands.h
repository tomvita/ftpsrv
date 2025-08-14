// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ftpsrv.h"
int ftp_custom_cmd_TID(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len);

extern const struct FtpSrvCustomCommand CUSTOM_COMMANDS[];
extern const unsigned CUSTOM_COMMANDS_SIZE;

#ifdef __cplusplus
}
#endif
