/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NcmContentStorage g_cs;

static char* hexIdToStr(char* buf, NcmContentId* id) {
    const u64 id_lower = __builtin_bswap64(*(u64*)id->c);
    const u64 id_upper = __builtin_bswap64(*(u64*)(id->c + 0x8));
    snprintf(buf, 0x21, "%016lx%016lx", id_lower, id_upper);
    return buf;
}

static NcmContentId parse_hex_key(const char* hex) {
    NcmContentId id = {0};
    char low[0x11] = {0};
    char upp[0x11] = {0};
    memcpy(low, hex, 0x10);
    memcpy(upp, hex + 0x10, 0x10);
    *(u64*)id.c = __builtin_bswap64(strtoull(low, NULL, 0x10));
    *(u64*)(id.c + 8) = __builtin_bswap64(strtoull(upp, NULL, 0x10));
    return id;
}

int ftp_vfs_ncm_open(struct FtpVfsNcmFile* f, const char* path, enum FtpVfsOpenMode mode) {
    if (mode != FtpVfsOpenMode_READ) {
        errno = EACCES;
        return -1;
    }

    const char* dilem = strchr(path, '/');
    if (!dilem) {
        errno = ENOENT;
        return -1;
    }

    Result rc;
    f->id = parse_hex_key(dilem + 1);
    if (R_FAILED(rc = ncmContentStorageGetSizeFromContentId(&g_cs, &f->size, &f->id))) {
        errno = ENOENT;
        return -1;
    }

    f->is_valid = 1;
    return 0;
}

int ftp_vfs_ncm_read(struct FtpVfsNcmFile* f, void* buf, size_t size) {
    #define min(x, y) ((x) < (y) ? (x) : (y))
    size = min(size, f->size - f->offset);

    Result rc;
    if (R_FAILED(rc = ncmContentStorageReadContentIdFile(&g_cs, buf, size, &f->id, f->offset))) {
        errno = EIO;
        return -1;
    }

    f->offset += size;
    return size;
}

int ftp_vfs_ncm_write(struct FtpVfsNcmFile* f, const void* buf, size_t size) {
    errno = EACCES;
    return -1;
}

int ftp_vfs_ncm_seek(struct FtpVfsNcmFile* f, size_t off) {
    f->offset = off;
    return 0;
}

int ftp_vfs_ncm_fstat(struct FtpVfsNcmFile* f, const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_size = f->size;
    st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    return 0;
}

int ftp_vfs_ncm_close(struct FtpVfsNcmFile* f) {
    if (!ftp_vfs_ncm_isfile_open(f)) {
        return -1;
    }

    memset(f, 0, sizeof(*f));
    return 0;
}

int ftp_vfs_ncm_isfile_open(struct FtpVfsNcmFile* f) {
    return f->is_valid;
}

int ftp_vfs_ncm_opendir(struct FtpVfsNcmDir* f, const char* path) {
    f->index = 0;
    f->is_valid = 1;
    return 0;
}

const char* ftp_vfs_ncm_readdir(struct FtpVfsNcmDir* f, struct FtpVfsNcmDirEntry* entry) {
    Result rc;
    s32 count;
    if (R_FAILED(rc = ncmContentStorageListContentId(&g_cs, &entry->id, 1, &count, f->index))) {
        errno = EIO;
        return NULL;
    }

    if (count <= 0) {
        return NULL;
    }

    f->index++;
    return hexIdToStr(entry->name, &entry->id);
}

int ftp_vfs_ncm_dirstat(struct FtpVfsNcmDir* f, const struct FtpVfsNcmDirEntry* entry, const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));

    Result rc;
    s64 out_size;
    if (R_FAILED(rc = ncmContentStorageGetSizeFromContentId(&g_cs, &out_size, &entry->id))) {
        errno = EIO;
        return -1;
    }

    st->st_nlink = 1;
    st->st_size = out_size;
    st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
    return 0;
}

int ftp_vfs_ncm_dirlstat(struct FtpVfsNcmDir* f, const struct FtpVfsNcmDirEntry* entry, const char* path, struct stat* st) {
    return ftp_vfs_ncm_dirstat(f, entry, path, st);
}

int ftp_vfs_ncm_closedir(struct FtpVfsNcmDir* f) {
    if (!ftp_vfs_ncm_isdir_open(f)) {
        return -1;
    }
    memset(f, 0, sizeof(*f));
    return 0;
}

int ftp_vfs_ncm_isdir_open(struct FtpVfsNcmDir* f) {
    return f->is_valid;
}

int ftp_vfs_ncm_stat(const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    return 0;
}

int ftp_vfs_ncm_lstat(const char* path, struct stat* st) {
    return ftp_vfs_ncm_stat(path, st);
}

int ftp_vfs_ncm_mkdir(const char* path) {
    errno = EACCES;
    return -1;
}

int ftp_vfs_ncm_unlink(const char* path) {
    errno = EACCES;
    return -1;
}

int ftp_vfs_ncm_rmdir(const char* path) {
    errno = EACCES;
    return -1;
}

int ftp_vfs_ncm_rename(const char* src, const char* dst) {
    errno = EACCES;
    return -1;
}

void ftp_vfs_ncm_init(void) {
    ncmOpenContentStorage(&g_cs, NcmStorageId_BuiltInSystem);
}

void ftp_vfs_ncm_exit(void) {
    ncmContentStorageClose(&g_cs);
}
