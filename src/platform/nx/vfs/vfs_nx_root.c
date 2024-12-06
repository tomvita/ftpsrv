/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

static const char* DEVICES[] = {
    "sdmc:",
    "image_nand:",
    "image_sd:",
    "switch:",
    "contents:",

    // "ns:",
    "firmware:",
    // "gc:",
    "save:",
};

int ftp_vfs_root_open(struct FtpVfsRootFile* f, const char* path, enum FtpVfsOpenMode mode) {
    return -1;
}

int ftp_vfs_root_read(struct FtpVfsRootFile* f, void* buf, size_t size) {
    return -1;
}

int ftp_vfs_root_write(struct FtpVfsRootFile* f, const void* buf, size_t size) {
    return -1;
}

int ftp_vfs_root_seek(struct FtpVfsRootFile* f, size_t off) {
    return -1;
}

int ftp_vfs_root_fstat(struct FtpVfsRootFile* f, const char* path, struct stat* st) {
    return -1;
}

int ftp_vfs_root_close(struct FtpVfsRootFile* f) {
    return -1;
}

int ftp_vfs_root_isfile_open(struct FtpVfsRootFile* f) {
    return -1;
}

int ftp_vfs_root_opendir(struct FtpVfsRootDir* f, const char* path) {
    f->index = 0;
    f->is_valid = 1;
    return 0;
}

const char* ftp_vfs_root_readdir(struct FtpVfsRootDir* f, struct FtpVfsRootDirEntry* entry) {
    if (f->index < sizeof(DEVICES) / sizeof(DEVICES[0])) {
        return DEVICES[f->index++];
    } else {
        return NULL;
    }
}

int ftp_vfs_root_dirstat(struct FtpVfsRootDir* f, const struct FtpVfsRootDirEntry* entry, const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    return 0;
}

int ftp_vfs_root_dirlstat(struct FtpVfsRootDir* f, const struct FtpVfsRootDirEntry* entry, const char* path, struct stat* st) {
    return ftp_vfs_root_dirstat(f, entry, path, st);
}

int ftp_vfs_root_closedir(struct FtpVfsRootDir* f) {
    if (!ftp_vfs_root_isdir_open(f)) {
        return -1;
    }
    memset(f, 0, sizeof(*f));
    return 0;
}

int ftp_vfs_root_isdir_open(struct FtpVfsRootDir* f) {
    return f->is_valid;
}

int ftp_vfs_root_stat(const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    return 0;
}

int ftp_vfs_root_lstat(const char* path, struct stat* st) {
    return ftp_vfs_root_stat(path, st);
}

int ftp_vfs_root_mkdir(const char* path) {
    return -1;
}

int ftp_vfs_root_unlink(const char* path) {
    return -1;
}

int ftp_vfs_root_rmdir(const char* path) {
    return -1;
}

int ftp_vfs_root_rename(const char* src, const char* dst) {
    return -1;
}
