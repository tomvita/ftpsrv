// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

struct FtpVfsRootFile {
    bool padding;
};

struct FtpVfsRootDir {
    size_t index;
    bool is_valid;
};

struct FtpVfsRootDirEntry {
    char buf[64];
};

int ftp_vfs_root_open(struct FtpVfsRootFile* f, const char* path, enum FtpVfsOpenMode mode);
int ftp_vfs_root_read(struct FtpVfsRootFile* f, void* buf, size_t size);
int ftp_vfs_root_write(struct FtpVfsRootFile* f, const void* buf, size_t size);
int ftp_vfs_root_seek(struct FtpVfsRootFile* f, size_t off);
int ftp_vfs_root_fstat(struct FtpVfsRootFile* f, const char* path, struct stat* st);
int ftp_vfs_root_close(struct FtpVfsRootFile* f);
int ftp_vfs_root_isfile_open(struct FtpVfsRootFile* f);

int ftp_vfs_root_opendir(struct FtpVfsRootDir* f, const char* path);
const char* ftp_vfs_root_readdir(struct FtpVfsRootDir* f, struct FtpVfsRootDirEntry* entry);
int ftp_vfs_root_dirstat(struct FtpVfsRootDir* f, const struct FtpVfsRootDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_root_dirlstat(struct FtpVfsRootDir* f, const struct FtpVfsRootDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_root_closedir(struct FtpVfsRootDir* f);
int ftp_vfs_root_isdir_open(struct FtpVfsRootDir* f);

int ftp_vfs_root_stat(const char* path, struct stat* st);
int ftp_vfs_root_lstat(const char* path, struct stat* st);
int ftp_vfs_root_mkdir(const char* path);
int ftp_vfs_root_unlink(const char* path);
int ftp_vfs_root_rmdir(const char* path);
int ftp_vfs_root_rename(const char* src, const char* dst);

#ifdef __cplusplus
}
#endif
