// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <dirent.h>

struct FtpVfsFile {
    FILE* fd;
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
