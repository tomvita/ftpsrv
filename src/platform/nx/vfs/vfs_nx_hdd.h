// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <dirent.h>
#include <switch.h>

struct VfsHddFile {
    int fd;
    int valid;
};

struct VfsHddDir {
    DIR* fd;
    size_t index;
    bool is_valid;
};

struct VfsHddDirEntry {
    struct dirent* d;
};

struct FtpVfs;
const extern struct FtpVfs g_vfs_hdd;

Result vfs_hdd_init(void);
void vfs_hdd_exit(void);

#ifdef __cplusplus
}
#endif
