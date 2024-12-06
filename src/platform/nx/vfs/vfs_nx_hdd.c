/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include "log/log.h"
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <usbhsfs.h>

static UsbHsFsDevice g_device[0x20];
static s32 g_count;

static const char* fix_path(const char* path) {
    return path + strlen("hdd:/");
}

static void poll_usbhsfs(void) {
    g_count = usbHsFsListMountedDevices(g_device, sizeof(g_device)/sizeof(g_device[0]));
}

static int vfs_hdd_open(void* user, const char* path, enum FtpVfsOpenMode mode) {
    poll_usbhsfs();

    path = fix_path(path);
    if (strncmp(path, "ums", strlen("ums"))) {
        return -1;
    }

    struct VfsHddFile* f = user;
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

    f->fd = open(path, flags, args);
    if (f->fd >= 0) {
        f->valid = 1;
    }
    return f->fd;
}

static int vfs_hdd_read(void* user, void* buf, size_t size) {
    struct VfsHddFile* f = user;
    return read(f->fd, buf, size);
}

static int vfs_hdd_write(void* user, const void* buf, size_t size) {
    struct VfsHddFile* f = user;
    return write(f->fd, buf, size);
}

static int vfs_hdd_seek(void* user, size_t off) {
    struct VfsHddFile* f = user;
    return lseek(f->fd, off, SEEK_SET);
}

static int vfs_hdd_fstat(void* user, const char* path, struct stat* st) {
    struct VfsHddFile* f = user;
    // fstat is not available with fatfs (fat32/exfat).
    if (fstat(f->fd, st) && errno == ENOSYS) {
        return stat(fix_path(path), st);
    }
    return 0;
}

static int vfs_hdd_isfile_open(void* user) {
    struct VfsHddFile* f = user;
    return f->valid && f->fd >= 0;
}

static int vfs_hdd_close(void* user) {
    struct VfsHddFile* f = user;
    if (!vfs_hdd_isfile_open(f)) {
        return -1;
    }
    int rc = close(f->fd);
    f->fd = -1;
    f->valid = 0;
    return rc;
}

static int vfs_hdd_opendir(void* user, const char* path) {
    poll_usbhsfs();
    struct VfsHddDir* f = user;
    path = fix_path(path);
    if (!strncmp(path, "ums", strlen("ums"))) {
        f->fd = opendir(path);
        if (!f->fd) {
            return -1;
        }
    }

    f->index = 0;
    f->is_valid = 1;
    return 0;
}

static const char* vfs_hdd_readdir(void* user, void* user_entry) {
    struct VfsHddDir* f = user;
    struct VfsHddDirEntry* entry = user_entry;

    if (f->fd) {
        entry->d = readdir(f->fd);
        if (!entry->d) {
            return NULL;
        }
        return entry->d->d_name;
    } else {
        if (f->index >= g_count) {
            return NULL;
        }
        return g_device[f->index++].name;
    }
}

static int vfs_hdd_dirstat(void* user, const void* user_entry, const char* path, struct stat* st) {
    struct VfsHddDir* f = user;
    path = fix_path(path);
    if (f->fd) {
        return stat(path, st);
    } else {
        memset(st, 0, sizeof(*st));
        st->st_nlink = 1;
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        return 0;
    }
}

static int vfs_hdd_isdir_open(void* user) {
    struct VfsHddDir* f = user;
    return f->is_valid;
}

static int vfs_hdd_closedir(void* user) {
    struct VfsHddDir* f = user;
    if (!vfs_hdd_isdir_open(f)) {
        return -1;
    }
    if (f->fd) {
        closedir(f->fd);
    }
    memset(f, 0, sizeof(*f));
    return 0;
}

static int vfs_hdd_stat(const char* path, struct stat* st) {
    poll_usbhsfs();
    path = fix_path(path);
    if (strncmp(path, "ums", strlen("ums"))) {
        return -1;
    }

    if (strlen(path) == 5) {
        memset(st, 0, sizeof(*st));
        st->st_nlink = 1;
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        return 0;
    } else {
        return stat(path, st);
    }
}

static int vfs_hdd_mkdir(const char* path) {
    poll_usbhsfs();
    path = fix_path(path);
    if (strlen(path) <= 5 || strncmp(path, "ums", strlen("ums"))) {
        return -1;
    }
    return mkdir(path, 0777);
}

static int vfs_hdd_unlink(const char* path) {
    poll_usbhsfs();
    path = fix_path(path);
    if (strlen(path) <= 5 || strncmp(path, "ums", strlen("ums"))) {
        return -1;
    }
    return unlink(path);
}

static int vfs_hdd_rmdir(const char* path) {
    poll_usbhsfs();
    path = fix_path(path);
    if (strlen(path) <= 5 || strncmp(path, "ums", strlen("ums"))) {
        return -1;
    }
    return rmdir(path);
}

static int vfs_hdd_rename(const char* src, const char* dst) {
    poll_usbhsfs();
    const char* path_src = fix_path(src);
    const char* path_dst = fix_path(dst);
    if (strlen(path_src) <= 5 || strncmp(path_src, "ums", strlen("ums"))) {
        return -1;
    }

    if (strncmp(path_src, path_dst, 6)) {
        return -1;
    }

    return rename(path_src, path_dst);
}

Result vfs_hdd_init(void) {
    return usbHsFsInitialize(0);
}

void vfs_hdd_exit(void) {
    usbHsFsExit();
}

const FtpVfs g_vfs_hdd = {
    .open = vfs_hdd_open,
    .read = vfs_hdd_read,
    .write = vfs_hdd_write,
    .seek = vfs_hdd_seek,
    .fstat = vfs_hdd_fstat,
    .close = vfs_hdd_close,
    .isfile_open = vfs_hdd_isfile_open,
    .opendir = vfs_hdd_opendir,
    .readdir = vfs_hdd_readdir,
    .dirstat = vfs_hdd_dirstat,
    .dirlstat = vfs_hdd_dirstat,
    .closedir = vfs_hdd_closedir,
    .isdir_open = vfs_hdd_isdir_open,
    .stat = vfs_hdd_stat,
    .lstat = vfs_hdd_stat,
    .mkdir = vfs_hdd_mkdir,
    .unlink = vfs_hdd_unlink,
    .rmdir = vfs_hdd_rmdir,
    .rename = vfs_hdd_rename,
};
