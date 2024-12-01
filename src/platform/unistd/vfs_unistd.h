// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/stat.h>
#include <dirent.h>

struct FtpVfsFile {
    int fd;
    int valid;
};

struct FtpVfsDir {
    DIR* fd;
};

struct FtpVfsDirEntry {
    struct dirent* buf;
};

#ifdef __cplusplus
}
#endif
