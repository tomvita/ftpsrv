// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
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
    u64 app_id;
    size_t path_off;
};

struct FtpVfsSaveFile {
    struct SavePathData data;
    struct FtpVfsFsFile fs_file;
    FsFileSystem fs;
    bool is_valid;
};

struct FtpVfsSaveDir {
    struct SavePathData data;
    FsSaveDataInfoReader r;
    FsFileSystem fs;
    struct FtpVfsFsDir fs_dir;
    s32 index;
    bool is_valid;
};

struct FtpVfsSaveDirEntry {
    union {
        struct {
            FsSaveDataInfo info;
            char name[512 + 128];
        };
        struct FtpVfsFsDirEntry fs_buf;
    };
};

int ftp_vfs_save_open(struct FtpVfsSaveFile* f, const char* path, enum FtpVfsOpenMode mode);
int ftp_vfs_save_read(struct FtpVfsSaveFile* f, void* buf, size_t size);
int ftp_vfs_save_write(struct FtpVfsSaveFile* f, const void* buf, size_t size);
int ftp_vfs_save_seek(struct FtpVfsSaveFile* f, size_t off);
int ftp_vfs_save_fstat(struct FtpVfsSaveFile* f, const char* path, struct stat* st);
int ftp_vfs_save_close(struct FtpVfsSaveFile* f);
int ftp_vfs_save_isfile_open(struct FtpVfsSaveFile* f);

int ftp_vfs_save_opendir(struct FtpVfsSaveDir* f, const char* path);
const char* ftp_vfs_save_readdir(struct FtpVfsSaveDir* f, struct FtpVfsSaveDirEntry* entry);
int ftp_vfs_save_dirstat(struct FtpVfsSaveDir* f, const struct FtpVfsSaveDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_save_dirlstat(struct FtpVfsSaveDir* f, const struct FtpVfsSaveDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_save_closedir(struct FtpVfsSaveDir* f);
int ftp_vfs_save_isdir_open(struct FtpVfsSaveDir* f);

int ftp_vfs_save_stat(const char* path, struct stat* st);
int ftp_vfs_save_lstat(const char* path, struct stat* st);
int ftp_vfs_save_mkdir(const char* path);
int ftp_vfs_save_unlink(const char* path);
int ftp_vfs_save_rmdir(const char* path);
int ftp_vfs_save_rename(const char* src, const char* dst);

void ftp_vfs_save_init(bool save_writable);
void ftp_vfs_save_exit(void);

#ifdef __cplusplus
}
#endif
