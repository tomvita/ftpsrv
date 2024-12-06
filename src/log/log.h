/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#ifndef LOG_TJ_H
#define LOG_TJ_H

#ifdef __cplusplus
extern "C" {
#endif

void log_file_write(const char* msg);
void log_file_fwrite(const char* fmt, ...);
void log_file_init(const char* path, const char* init_msg);
void log_file_exit(void);

#ifdef __cplusplus
}
#endif

#endif // LOG_TJ_H
