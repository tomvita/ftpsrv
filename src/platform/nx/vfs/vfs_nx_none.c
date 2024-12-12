#include "ftpsrv_vfs.h"
#include "errno.h"

static int set_errno_and_return_minus1(void) {
    errno = ENOENT;
    return -1;
}

static int vfs_none_open(void* user, const char* path, enum FtpVfsOpenMode mode) {
    return set_errno_and_return_minus1();
}

static int vfs_none_read(void* user, void* buf, size_t size) {
    return set_errno_and_return_minus1();
}

static int vfs_none_write(void* user, const void* buf, size_t size) {
    return set_errno_and_return_minus1();
}

static int vfs_none_seek(void* user, const void* buf, size_t size, size_t off) {
    return set_errno_and_return_minus1();
}

static int vfs_none_isfile_open(void* user) {
    return set_errno_and_return_minus1();
}

static int vfs_none_close(void* user) {
    return set_errno_and_return_minus1();
}

static int vfs_none_opendir(void* user, const char* path) {
    return set_errno_and_return_minus1();
}

static const char* vfs_none_readdir(void* user, void* user_entry) {
    return NULL;
}

static int vfs_none_dirlstat(void* user, const void* user_entry, const char* path, struct stat* st) {
    return set_errno_and_return_minus1();
}

static int vfs_none_isdir_open(void* user) {
    return 0;
}

static int vfs_none_closedir(void* user) {
   return set_errno_and_return_minus1();
}

static int vfs_none_stat(const char* path, struct stat* st) {
    return set_errno_and_return_minus1();
}

static int vfs_none_mkdir(const char* path) {
    return set_errno_and_return_minus1();
}

static int vfs_none_unlink(const char* path) {
    return set_errno_and_return_minus1();
}

static int vfs_none_rmdir(const char* path) {
    return set_errno_and_return_minus1();
}

static int vfs_none_rename(const char* src, const char* dst) {
    return set_errno_and_return_minus1();
}

const FtpVfs g_vfs_none = {
    .open = vfs_none_open,
    .read = vfs_none_read,
    .write = vfs_none_write,
    .seek = vfs_none_seek,
    .close = vfs_none_close,
    .isfile_open = vfs_none_isfile_open,
    .opendir = vfs_none_opendir,
    .readdir = vfs_none_readdir,
    .dirlstat = vfs_none_dirlstat,
    .closedir = vfs_none_closedir,
    .isdir_open = vfs_none_isdir_open,
    .stat = vfs_none_stat,
    .lstat = vfs_none_stat,
    .mkdir = vfs_none_mkdir,
    .unlink = vfs_none_unlink,
    .rmdir = vfs_none_rmdir,
    .rename = vfs_none_rename,
};
