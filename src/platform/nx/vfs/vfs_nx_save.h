// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <switch.h>
#include "vfs_nx_fs.h"

enum SaveDirType {
    SaveDirType_Invalid,
    SaveDirType_Root,
    SaveDirType_User,
    SaveDirType_App,
};

struct SavePathData {
    enum SaveDirType type;
    AccountUid uid;
    FsSaveDataType data_type;
    FsSaveDataSpaceId space_id;
    u64 app_id;
    size_t path_off;
};

struct VfsSaveFile {
    struct SavePathData data;
    struct VfsFsFile fs_file;
    FsFileSystem fs;
    bool is_valid;
};

struct VfsSaveDir {
    struct SavePathData data;
    FsSaveDataInfoReader r;
    FsFileSystem fs;
    struct VfsFsDir fs_dir;
    s32 index;
    bool is_valid;
};

struct VfsSaveDirEntry {
    union {
        struct {
            FsSaveDataInfo info;
            char name[512 + 128];
        };
        struct VfsFsDirEntry fs_buf;
    };
};

void vfs_save_init(bool save_writable);
void vfs_save_exit(void);

struct FtpVfs;
const extern struct FtpVfs g_vfs_save;

#ifdef __cplusplus
}
#endif
