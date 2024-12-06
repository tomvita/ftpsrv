// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>
#include <switch.h>

struct FtpVfsFsFile {
    FsFile fd;
    s64 off;
    s64 chunk_size;
    bool is_valid;
#if VFS_NX_BUFFER_WRITES
    s64 buf_off;
    u8 buf[1024*1024*1];
#endif
};

struct FtpVfsFsDir {
    FsDir dir;
    bool is_valid;
};

struct FtpVfsFsDirEntry {
    FsDirectoryEntry buf;
};

int vfs_fs_internal_open(FsFileSystem* fs, struct FtpVfsFsFile* f, const char nxpath[static FS_MAX_PATH], enum FtpVfsOpenMode mode);
int vfs_fs_internal_read(struct FtpVfsFsFile* f, void* buf, size_t size);
int vfs_fs_internal_write(struct FtpVfsFsFile* f, const void* buf, size_t size);
int vfs_fs_internal_seek(struct FtpVfsFsFile* f, size_t off);
int vfs_fs_internal_fstat(FsFileSystem* fs, struct FtpVfsFsFile* f, const char nxpath[static FS_MAX_PATH], struct stat* st);
int vfs_fs_internal_close(struct FtpVfsFsFile* f);
int vfs_fs_internal_isfile_open(struct FtpVfsFsFile* f);

int vfs_fs_internal_opendir(FsFileSystem* fs, struct FtpVfsFsDir* f, const char nxpath[static FS_MAX_PATH]);
const char* vfs_fs_internal_readdir(struct FtpVfsFsDir* f, struct FtpVfsFsDirEntry* entry);
int vfs_fs_internal_dirstat(FsFileSystem* fs, struct FtpVfsFsDir* f, const struct FtpVfsFsDirEntry* entry, const char nxpath[static FS_MAX_PATH], struct stat* st);
int vfs_fs_internal_dirlstat(FsFileSystem* fs, struct FtpVfsFsDir* f, const struct FtpVfsFsDirEntry* entry, const char nxpath[static FS_MAX_PATH], struct stat* st);
int vfs_fs_internal_closedir(struct FtpVfsFsDir* f);
int vfs_fs_internal_isdir_open(struct FtpVfsFsDir* f);

int vfs_fs_internal_stat(FsFileSystem* fs, const char nxpath[static FS_MAX_PATH], struct stat* st);
int vfs_fs_internal_lstat(FsFileSystem* fs, const char nxpath[static FS_MAX_PATH], struct stat* st);
int vfs_fs_internal_mkdir(FsFileSystem* fs, const char nxpath[static FS_MAX_PATH]);
int vfs_fs_internal_unlink(FsFileSystem* fs, const char nxpath[static FS_MAX_PATH]);
int vfs_fs_internal_rmdir(FsFileSystem* fs, const char nxpath[static FS_MAX_PATH]);
int vfs_fs_internal_rename(FsFileSystem* fs, const char nxpath_src[static FS_MAX_PATH], const char nxpath_dst[static FS_MAX_PATH]);

int ftp_vfs_fs_open(struct FtpVfsFsFile* f, const char* path, enum FtpVfsOpenMode mode);
int ftp_vfs_fs_read(struct FtpVfsFsFile* f, void* buf, size_t size);
int ftp_vfs_fs_write(struct FtpVfsFsFile* f, const void* buf, size_t size);
int ftp_vfs_fs_seek(struct FtpVfsFsFile* f, size_t off);
int ftp_vfs_fs_fstat(struct FtpVfsFsFile* f, const char* path, struct stat* st);
int ftp_vfs_fs_close(struct FtpVfsFsFile* f);
int ftp_vfs_fs_isfile_open(struct FtpVfsFsFile* f);

int ftp_vfs_fs_opendir(struct FtpVfsFsDir* f, const char* path);
const char* ftp_vfs_fs_readdir(struct FtpVfsFsDir* f, struct FtpVfsFsDirEntry* entry);
int ftp_vfs_fs_dirstat(struct FtpVfsFsDir* f, const struct FtpVfsFsDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_fs_dirlstat(struct FtpVfsFsDir* f, const struct FtpVfsFsDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_fs_closedir(struct FtpVfsFsDir* f);
int ftp_vfs_fs_isdir_open(struct FtpVfsFsDir* f);

int ftp_vfs_fs_stat(const char* path, struct stat* st);
int ftp_vfs_fs_lstat(const char* path, struct stat* st);
int ftp_vfs_fs_mkdir(const char* path);
int ftp_vfs_fs_unlink(const char* path);
int ftp_vfs_fs_rmdir(const char* path);
int ftp_vfs_fs_rename(const char* src, const char* dst);

#ifdef __cplusplus
}
#endif
