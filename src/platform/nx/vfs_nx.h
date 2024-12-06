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
#include "vfs/vfs_nx_save.h"
#if USE_USBHSFS
#include "vfs/vfs_nx_hdd.h"
#endif

enum VFS_TYPE {
    VFS_TYPE_NONE,
    VFS_TYPE_ROOT, // list root devices
    VFS_TYPE_FS, // list native fs devices
    VFS_TYPE_SAVE, // list xci, uses ncm
#if USE_USBHSFS
    VFS_TYPE_HDD, // list xci, uses ncm
#endif
};

struct FtpVfsFile {
    enum VFS_TYPE type;
    union {
        struct VfsRootFile root;
        struct VfsFsFile fs;
        struct VfsSaveFile save;
#if USE_USBHSFS
        struct VfsHddFile usbhsfs;
#endif
    };
};

struct FtpVfsDir {
    enum VFS_TYPE type;
    union {
        struct VfsRootDir root;
        struct VfsFsDir fs;
        struct VfsSaveDir save;
#if USE_USBHSFS
        struct VfsHddDir usbhsfs;
#endif
    };
};

struct FtpVfsDirEntry {
    enum VFS_TYPE type;
    union {
        struct VfsRootDirEntry root;
        struct VfsFsDirEntry fs;
        struct VfsSaveDirEntry save;
#if USE_USBHSFS
        struct VfsHddDirEntry usbhsfs;
#endif
    };
};

struct AppName {
    char str[0x200];
};

typedef struct FtpVfs {
    // vfs_file
    int (*open)(void* user, const char* path, enum FtpVfsOpenMode mode);
    int (*read)(void* user, void* buf, size_t size);
    int (*write)(void* user, const void* buf, size_t size);
    int (*seek)(void* user, size_t off);
    int (*fstat)(void* user, const char* path, struct stat* st);
    int (*close)(void* user);
    int (*isfile_open)(void* user);

    // vfs_dir
    int (*opendir)(void* user, const char* path);
    const char* (*readdir)(void* user, void* user_entry);
    int (*dirstat)(void* user, const void* user_entry, const char* path, struct stat* st);
    int (*dirlstat)(void* user, const void* user_entry, const char* path, struct stat* st);
    int (*closedir)(void* user);
    int (*isdir_open)(void* user);

    // vfs_sys
    int (*stat)(const char* path, struct stat* st);
    int (*lstat)(const char* path, struct stat* st);
    int (*mkdir)(const char* path);
    int (*unlink)(const char* path);
    int (*rmdir)(const char* path);
    int (*rename)(const char* src, const char* dst);
} FtpVfs;

void vfs_nx_init(bool enable_devices, bool save_writable, bool mount_bis);
void vfs_nx_exit(void);
void vfs_nx_add_device(const char* name, enum VFS_TYPE type);

Result get_app_name(u64 app_id, NcmContentId* id, struct AppName* name);

#ifdef __cplusplus
}
#endif
