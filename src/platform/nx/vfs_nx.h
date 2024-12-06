// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

#include "vfs/vfs_nx_root.h"
#include "vfs/vfs_nx_fs.h"
#include "vfs/vfs_nx_ncm.h"
#include "vfs/vfs_nx_save.h"

enum VFS_TYPE {
    VFS_TYPE_NONE,
    VFS_TYPE_ROOT, // list root devices
    VFS_TYPE_FS, // list native fs devices
    VFS_TYPE_NCM, // list nca's, uses ncm
    VFS_TYPE_SAVE, // list xci, uses ncm
};

struct FtpVfsFile {
    enum VFS_TYPE type;
    union {
        struct FtpVfsRootFile root;
        struct FtpVfsFsFile fs;
        struct FtpVfsNcmFile ncm;
        struct FtpVfsSaveFile save;
    };
};


struct FtpVfsDir {
    enum VFS_TYPE type;
    union {
        struct FtpVfsRootDir root;
        struct FtpVfsFsDir fs;
        struct FtpVfsNcmDir ncm;
        struct FtpVfsSaveDir save;
    };
};

struct FtpVfsDirEntry {
    enum VFS_TYPE type;
    union {
        struct FtpVfsRootDirEntry root;
        struct FtpVfsFsDirEntry fs;
        struct FtpVfsNcmDirEntry ncm;
        struct FtpVfsSaveDirEntry save;
    };
};

struct AppName {
    char str[0x200];
};

void vfs_nx_init(bool enable_devices, bool save_writable);
void vfs_nx_exit(void);

Result get_app_name(u64 app_id, NcmContentId* id, struct AppName* name);

#ifdef __cplusplus
}
#endif
