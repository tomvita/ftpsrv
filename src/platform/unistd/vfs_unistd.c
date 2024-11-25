/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"

#include <stddef.h>
#include <sys/stat.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#if defined(HAVE_LSTAT) && !HAVE_LSTAT
    #define lstat stat
#endif

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    int flags = 0, args = 0;

    switch (mode) {
        case FtpVfsOpenMode_READ:
            flags = O_RDONLY;
            args = 0;
            break;
        case FtpVfsOpenMode_WRITE:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            args = 0666;
            break;
        case FtpVfsOpenMode_APPEND:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            args = 0666;
            break;
    }

    return f->fd = open(path, flags, args);
}

int ftp_vfs_read(struct FtpVfsFile* f, void* buf, size_t size) {
    return read(f->fd, buf, size);
}

int ftp_vfs_write(struct FtpVfsFile* f, const void* buf, size_t size) {
    return write(f->fd, buf, size);
}

int ftp_vfs_seek(struct FtpVfsFile* f, size_t off) {
    return lseek(f->fd, off, SEEK_SET);
}

int ftp_vfs_fstat(struct FtpVfsFile* f, const char* path, struct stat* st) {
    return fstat(f->fd, st);
}

int ftp_vfs_close(struct FtpVfsFile* f) {
    int rc = 0;
    if (ftp_vfs_isfile_open(f)) {
        rc = close(f->fd);
        f->fd = -1;
    }
    return rc;
}

int ftp_vfs_isfile_open(struct FtpVfsFile* f) {
    return f->fd >= 0;
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
    int rc = 0;
    if (ftp_vfs_isdir_open(f)) {
        rc = closedir(f->fd);
        f->fd = NULL;
    }
    return rc;
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
    return unlink(path);
}

int ftp_vfs_rmdir(const char* path) {
    return rmdir(path);
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
