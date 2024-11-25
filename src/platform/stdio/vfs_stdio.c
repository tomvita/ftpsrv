/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"

#include <stddef.h>
#include <sys/stat.h>

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>

#if defined(HAVE_LSTAT) && !HAVE_LSTAT
    #define lstat stat
#endif

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    switch (mode) {
        case FtpVfsOpenMode_READ:
            f->fd = fopen(path, "rb");
            break;
        case FtpVfsOpenMode_WRITE:
            f->fd = fopen(path, "wb");
            break;
        case FtpVfsOpenMode_APPEND:
            f->fd = fopen(path, "wb+");
            break;
    }

    if (!f->fd) {
        return -1;
    } else {
        return 0;
    }
}

int ftp_vfs_read(struct FtpVfsFile* f, void* buf, size_t size) {
    return fread(buf, 1, size, f->fd);
}

int ftp_vfs_write(struct FtpVfsFile* f, const void* buf, size_t size) {
    return fwrite(buf, 1, size, f->fd);
}

int ftp_vfs_seek(struct FtpVfsFile* f, size_t off) {
    return fseek(f->fd, off, SEEK_SET);
}

int ftp_vfs_fstat(struct FtpVfsFile* f, const char* path, struct stat* st) {
    return fstat(fileno(f->fd), st);
}

int ftp_vfs_close(struct FtpVfsFile* f) {
    if (!ftp_vfs_isfile_open(f)) {
        return -1;
    }
    int rc = fclose(f->fd);
    f->fd = NULL;
    return rc;
}

int ftp_vfs_isfile_open(struct FtpVfsFile* f) {
    return f->fd != NULL;
}

int ftp_vfs_opendir(struct FtpVfsDir* f, const char* path) {
    f->fd = opendir(path);
    if (!f->fd) {
        return -1;
    }
    return 0;
}

const char* ftp_vfs_readdir(struct FtpVfsDir* f, struct FtpVfsDirEntry* entry) {
    entry->buf = readdir(f->fd);
    if (!entry->buf) {
        return NULL;
    }
    return entry->buf->d_name;
}

int ftp_vfs_dirstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return stat(path, st);
}

int ftp_vfs_dirlstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return lstat(path, st);
}

int ftp_vfs_closedir(struct FtpVfsDir* f) {
    if (ftp_vfs_isdir_open(f)) {
        closedir(f->fd);
        f->fd = NULL;
    }
    return 0;
}

int ftp_vfs_isdir_open(struct FtpVfsDir* f) {
    return f->fd != NULL;
}

int ftp_vfs_stat(const char* path, struct stat* st) {
    return stat(path, st);
}

int ftp_vfs_lstat(const char* path, struct stat* st) {
    return lstat(path, st);
}

int ftp_vfs_mkdir(const char* path) {
    return mkdir(path, 0777);
}

int ftp_vfs_unlink(const char* path) {
    return remove(path);
}

int ftp_vfs_rmdir(const char* path) {
    return remove(path);
}

int ftp_vfs_rename(const char* src, const char* dst) {
    return rename(src, dst);
}

int ftp_vfs_readlink(const char* path, char* buf, size_t buflen) {
#if defined(HAVE_READLINK) && HAVE_READLINK
    return readlink(path, buf, buflen);
#else
    return -1;
#endif
}

#if defined(HAVE_GETPWUID) && HAVE_GETPWUID
#include <pwd.h>
const char* ftp_vfs_getpwuid(const struct stat* st) {
    const struct passwd *pw = getpwuid(st->st_uid);
    return pw ? pw->pw_name : "unknown";
}
#else
const char* ftp_vfs_getpwuid(const struct stat* st) {
    return "unknown";
}
#endif

#if defined(HAVE_GETGRGID) && HAVE_GETGRGID
#include <grp.h>
const char* ftp_vfs_getgrgid(const struct stat* st) {
    const struct group *gr = getgrgid(st->st_gid);
    return gr ? gr->gr_name : "unknown";
}
#else
const char* ftp_vfs_getgrgid(const struct stat* st) {
    return "unknown";
}
#endif
