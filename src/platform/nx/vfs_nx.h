// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

struct FtpVfsFile {
    FsFile fd;
    s64 off;
    s64 chunk_size;
    bool is_valid;
#if VFS_NX_BUFFER_WRITES
    s64 buf_off;
    u8 buf[1024*1024*1];
#endif
};

struct FtpVfsDir {
    FsDir dir;
    bool is_valid;
};

struct FtpVfsDirEntry {
    FsDirectoryEntry buf;
};

#ifdef __cplusplus
}
#endif
