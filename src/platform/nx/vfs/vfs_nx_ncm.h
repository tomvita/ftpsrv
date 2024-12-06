// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

struct FtpVfsNcmFile {
    NcmContentId id;
    s64 size;
    s64 offset;
    bool is_valid;
};

struct FtpVfsNcmDir {
    s32 index;
    bool is_valid;
};

struct FtpVfsNcmDirEntry {
    NcmContentId id;
    char name[128];
};

int ftp_vfs_ncm_open(struct FtpVfsNcmFile* f, const char* path, enum FtpVfsOpenMode mode);
int ftp_vfs_ncm_read(struct FtpVfsNcmFile* f, void* buf, size_t size);
int ftp_vfs_ncm_write(struct FtpVfsNcmFile* f, const void* buf, size_t size);
int ftp_vfs_ncm_seek(struct FtpVfsNcmFile* f, size_t off);
int ftp_vfs_ncm_fstat(struct FtpVfsNcmFile* f, const char* path, struct stat* st);
int ftp_vfs_ncm_close(struct FtpVfsNcmFile* f);
int ftp_vfs_ncm_isfile_open(struct FtpVfsNcmFile* f);

int ftp_vfs_ncm_opendir(struct FtpVfsNcmDir* f, const char* path);
const char* ftp_vfs_ncm_readdir(struct FtpVfsNcmDir* f, struct FtpVfsNcmDirEntry* entry);
int ftp_vfs_ncm_dirstat(struct FtpVfsNcmDir* f, const struct FtpVfsNcmDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_ncm_dirlstat(struct FtpVfsNcmDir* f, const struct FtpVfsNcmDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_ncm_closedir(struct FtpVfsNcmDir* f);
int ftp_vfs_ncm_isdir_open(struct FtpVfsNcmDir* f);

int ftp_vfs_ncm_stat(const char* path, struct stat* st);
int ftp_vfs_ncm_lstat(const char* path, struct stat* st);
int ftp_vfs_ncm_mkdir(const char* path);
int ftp_vfs_ncm_unlink(const char* path);
int ftp_vfs_ncm_rmdir(const char* path);
int ftp_vfs_ncm_rename(const char* src, const char* dst);

void ftp_vfs_ncm_init(void);
void ftp_vfs_ncm_exit(void);

#ifdef __cplusplus
}
#endif
