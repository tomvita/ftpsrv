// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#ifndef FTP_SRV_VFS_H
#define FTP_SRV_VFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/stat.h>

enum FtpVfsOpenMode {
    FtpVfsOpenMode_READ,
    FtpVfsOpenMode_WRITE, // create and truncate is implicitly implied
    FtpVfsOpenMode_APPEND,
};

struct FtpVfsFile;
struct FtpVfsDir;
struct FtpVfsDirEntry;

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode);
int ftp_vfs_read(struct FtpVfsFile* f, void* buf, size_t size);
int ftp_vfs_write(struct FtpVfsFile* f, const void* buf, size_t size);
int ftp_vfs_seek(struct FtpVfsFile* f, size_t off);
int ftp_vfs_fstat(struct FtpVfsFile* f, const char* path, struct stat* st);
int ftp_vfs_close(struct FtpVfsFile* f);
int ftp_vfs_isfile_open(struct FtpVfsFile* f);

int ftp_vfs_opendir(struct FtpVfsDir* f, const char* path);
const char* ftp_vfs_readdir(struct FtpVfsDir* f, struct FtpVfsDirEntry* entry);
int ftp_vfs_dirstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_dirlstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st);
int ftp_vfs_closedir(struct FtpVfsDir* f);
int ftp_vfs_isdir_open(struct FtpVfsDir* f);

int ftp_vfs_stat(const char* path, struct stat* st);
int ftp_vfs_lstat(const char* path, struct stat* st);
int ftp_vfs_mkdir(const char* path);
int ftp_vfs_unlink(const char* path);
int ftp_vfs_rmdir(const char* path);
int ftp_vfs_rename(const char* src, const char* dst);
int ftp_vfs_readlink(const char* path, char* buf, size_t buflen);

const char* ftp_vfs_getpwuid(const struct stat* st);
const char* ftp_vfs_getgrgid(const struct stat* st);

#ifdef FTP_VFS_HEADER
    #include FTP_VFS_HEADER
#else
    #error FTP_VFS_HEADER not set to the header file path!
#endif

#ifdef __cplusplus
}
#endif

#endif // FTP_SRV_VFS_H
