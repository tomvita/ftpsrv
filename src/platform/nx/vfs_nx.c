/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

static bool g_enabled_devices = false;
static NcmContentStorage g_cs[2];
static NcmContentMetaDatabase g_db[2];

static bool is_path(const char* path, const char* name) {
    if (path[0] == '/') {
        return !strncmp(path + 1, name, strlen(name));
    } else {
        return !strncmp(path, name, strlen(name));
    }
}

static enum VFS_TYPE get_type(const char* path) {
    if (!g_enabled_devices) {
        return VFS_TYPE_FS;
    } else {
        if (!path || !strcmp(path, "/")) {
            return VFS_TYPE_ROOT;
        } else if (strchr(path, ':')) {
            if (is_path(path, "firmware:")) {
                return VFS_TYPE_NCM;
            }  else if (is_path(path, "save:")) {
                return VFS_TYPE_SAVE;
            } else {
                return VFS_TYPE_FS;
            }
        }

        return VFS_TYPE_NONE;
    }
}

static const char* fix_path(const char* path, enum VFS_TYPE type) {
    switch (type) {
        case VFS_TYPE_NONE: return NULL;
        case VFS_TYPE_ROOT: return "/";
        default:
            if (strchr(path, ':') && path[0] == '/') {
                return path + 1;
            }
            return path;
    }
}

static int set_errno_and_return_minus1(void) {
    errno = ENOENT;
    return -1;
}

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    f->type = get_type(path);
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_open(&f->root, fix_path(path, f->type), mode);
        case VFS_TYPE_FS: return ftp_vfs_fs_open(&f->fs, fix_path(path, f->type), mode);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_open(&f->ncm, fix_path(path, f->type), mode);
        case VFS_TYPE_SAVE: return ftp_vfs_save_open(&f->save, fix_path(path, f->type), mode);
    }
}

int ftp_vfs_read(struct FtpVfsFile* f, void* buf, size_t size) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_read(&f->root, buf, size);
        case VFS_TYPE_FS: return ftp_vfs_fs_read(&f->fs, buf, size);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_read(&f->ncm, buf, size);
        case VFS_TYPE_SAVE: return ftp_vfs_save_read(&f->save, buf, size);
    }
}

int ftp_vfs_write(struct FtpVfsFile* f, const void* buf, size_t size) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_write(&f->root, buf, size);
        case VFS_TYPE_FS: return ftp_vfs_fs_write(&f->fs, buf, size);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_write(&f->ncm, buf, size);
        case VFS_TYPE_SAVE: return ftp_vfs_save_write(&f->save, buf, size);
    }
}

int ftp_vfs_seek(struct FtpVfsFile* f, size_t off) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_seek(&f->root, off);
        case VFS_TYPE_FS: return ftp_vfs_fs_seek(&f->fs, off);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_seek(&f->ncm, off);
        case VFS_TYPE_SAVE: return ftp_vfs_save_seek(&f->save, off);
    }
}

int ftp_vfs_fstat(struct FtpVfsFile* f, const char* path, struct stat* st) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_fstat(&f->root, fix_path(path, f->type), st);
        case VFS_TYPE_FS: return ftp_vfs_fs_fstat(&f->fs, fix_path(path, f->type), st);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_fstat(&f->ncm, fix_path(path, f->type), st);
        case VFS_TYPE_SAVE: return ftp_vfs_save_fstat(&f->save, fix_path(path, f->type), st);
    }
}

int ftp_vfs_close(struct FtpVfsFile* f) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_close(&f->root);
        case VFS_TYPE_FS: return ftp_vfs_fs_close(&f->fs);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_close(&f->ncm);
        case VFS_TYPE_SAVE: return ftp_vfs_save_close(&f->save);
    }
}

int ftp_vfs_isfile_open(struct FtpVfsFile* f) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return 0;
        case VFS_TYPE_ROOT: return ftp_vfs_root_isfile_open(&f->root);
        case VFS_TYPE_FS: return ftp_vfs_fs_isfile_open(&f->fs);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_isfile_open(&f->ncm);
        case VFS_TYPE_SAVE: return ftp_vfs_save_isfile_open(&f->save);
    }
}

int ftp_vfs_opendir(struct FtpVfsDir* f, const char* path) {
    f->type = get_type(path);
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_opendir(&f->root, fix_path(path, f->type));
        case VFS_TYPE_FS: return ftp_vfs_fs_opendir(&f->fs, fix_path(path, f->type));
        case VFS_TYPE_NCM: return ftp_vfs_ncm_opendir(&f->ncm, fix_path(path, f->type));
        case VFS_TYPE_SAVE: return ftp_vfs_save_opendir(&f->save, fix_path(path, f->type));
    }
}

const char* ftp_vfs_readdir(struct FtpVfsDir* f, struct FtpVfsDirEntry* entry) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return NULL;
        case VFS_TYPE_ROOT: return ftp_vfs_root_readdir(&f->root, &entry->root);
        case VFS_TYPE_FS: return ftp_vfs_fs_readdir(&f->fs, &entry->fs);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_readdir(&f->ncm, &entry->ncm);
        case VFS_TYPE_SAVE: return ftp_vfs_save_readdir(&f->save, &entry->save);
    }
}

int ftp_vfs_dirstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_dirstat(&f->root, &entry->root, fix_path(path, f->type), st);
        case VFS_TYPE_FS: return ftp_vfs_fs_dirstat(&f->fs, &entry->fs, fix_path(path, f->type), st);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_dirstat(&f->ncm, &entry->ncm, fix_path(path, f->type), st);
        case VFS_TYPE_SAVE: return ftp_vfs_save_dirstat(&f->save, &entry->save, fix_path(path, f->type), st);
    }
}

int ftp_vfs_dirlstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return ftp_vfs_dirstat(f, entry, path, st);
}

int ftp_vfs_closedir(struct FtpVfsDir* f) {
    const enum VFS_TYPE type = f->type;
    f->type = VFS_TYPE_NONE;

    switch (type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_closedir(&f->root);
        case VFS_TYPE_FS: return ftp_vfs_fs_closedir(&f->fs);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_closedir(&f->ncm);
        case VFS_TYPE_SAVE: return ftp_vfs_save_closedir(&f->save);
    }
}

int ftp_vfs_isdir_open(struct FtpVfsDir* f) {
    switch (f->type) {
        default: case VFS_TYPE_NONE: return 0;
        case VFS_TYPE_ROOT: return ftp_vfs_root_isdir_open(&f->root);
        case VFS_TYPE_FS: return ftp_vfs_fs_isdir_open(&f->fs);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_isdir_open(&f->ncm);
        case VFS_TYPE_SAVE: return ftp_vfs_save_isdir_open(&f->save);
    }
}

int ftp_vfs_stat(const char* path, struct stat* st) {
    enum VFS_TYPE type = get_type(path);
    const char* dilem = strchr(path, ':');

    if (type != VFS_TYPE_NONE && dilem && (!strcmp(dilem, ":") || !strcmp(dilem, ":/"))) {
        return ftp_vfs_root_stat(path, st);
    }

    switch (type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_stat(fix_path(path, type), st);
        case VFS_TYPE_FS: return ftp_vfs_fs_stat(fix_path(path, type), st);
        case VFS_TYPE_NCM: return ftp_vfs_ncm_stat(fix_path(path, type), st);
        case VFS_TYPE_SAVE: return ftp_vfs_save_stat(fix_path(path, type), st);
    }
}

int ftp_vfs_lstat(const char* path, struct stat* st) {
    return ftp_vfs_stat(path, st);
}

int ftp_vfs_mkdir(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    switch (type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_mkdir(fix_path(path, type));
        case VFS_TYPE_FS: return ftp_vfs_fs_mkdir(fix_path(path, type));
        case VFS_TYPE_NCM: return ftp_vfs_ncm_mkdir(fix_path(path, type));
        case VFS_TYPE_SAVE: return ftp_vfs_save_mkdir(fix_path(path, type));
    }
}

int ftp_vfs_unlink(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    switch (type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_unlink(fix_path(path, type));
        case VFS_TYPE_FS: return ftp_vfs_fs_unlink(fix_path(path, type));
        case VFS_TYPE_NCM: return ftp_vfs_ncm_unlink(fix_path(path, type));
        case VFS_TYPE_SAVE: return ftp_vfs_save_unlink(fix_path(path, type));
    }
}

int ftp_vfs_rmdir(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    switch (type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_rmdir(fix_path(path, type));
        case VFS_TYPE_FS: return ftp_vfs_fs_rmdir(fix_path(path, type));
        case VFS_TYPE_NCM: return ftp_vfs_ncm_rmdir(fix_path(path, type));
        case VFS_TYPE_SAVE: return ftp_vfs_save_rmdir(fix_path(path, type));
    }
}

int ftp_vfs_rename(const char* src, const char* dst) {
    const enum VFS_TYPE src_type = get_type(src);
    const enum VFS_TYPE dst_type = get_type(dst);
    if (src_type != dst_type) {
        errno = EXDEV; // this will do for the error.
        return -1;
    }

    switch (src_type) {
        default: case VFS_TYPE_NONE: return set_errno_and_return_minus1();
        case VFS_TYPE_ROOT: return ftp_vfs_root_rename(fix_path(src, src_type), fix_path(dst, dst_type));
        case VFS_TYPE_FS: return ftp_vfs_fs_rename(fix_path(src, src_type), fix_path(dst, dst_type));
        case VFS_TYPE_NCM: return ftp_vfs_ncm_rename(fix_path(src, src_type), fix_path(dst, dst_type));
        case VFS_TYPE_SAVE: return ftp_vfs_save_rename(fix_path(src, src_type), fix_path(dst, dst_type));
    }
}

int ftp_vfs_readlink(const char* path, char* buf, size_t buflen) {
    return -1;
}

const char* ftp_vfs_getpwuid(const struct stat* st) {
    return "unknown";
}

const char* ftp_vfs_getgrgid(const struct stat* st) {
    return "unknown";
}

Result get_app_name(u64 app_id, NcmContentId* id, struct AppName* name) {
    Result rc;

    for (int i = 0; i < 2; i++) {
        NcmContentMetaKey key;
        s32 entries_total;
        s32 entries_written;
        if (R_FAILED(rc = ncmContentMetaDatabaseList(&g_db[i], &entries_total, &entries_written, &key, 1, NcmContentMetaType_Application, app_id, app_id, app_id, NcmContentInstallType_Full))) {
            // printf("failed to list ncm\n");
            continue;
        }

        if (R_FAILED(rc = ncmContentMetaDatabaseGetContentIdByType(&g_db[i], id, &key, NcmContentType_Control))) {
            // printf("failed to list id\n");
            continue;
        }

        char nxpath[FS_MAX_PATH] = {0};
        if (R_FAILED(rc = ncmContentStorageGetPath(&g_cs[i], nxpath, sizeof(nxpath), id))) {
            // printf("failed to get path: %s \n", nxpath);
            continue;
        }

        FsFileSystem fs;
        if (R_FAILED(rc = fsOpenFileSystemWithId(&fs, key.id, FsFileSystemType_ContentControl, nxpath, FsContentAttributes_None))) {
            // printf("failed to open fs: %s \n", nxpath);
            continue;
        }

        strcpy(nxpath, "/control.nacp");
        FsFile file;
        if (R_FAILED(rc = fsFsOpenFile(&fs, nxpath, FsOpenMode_Read, &file))) {
            // printf("failed to open file: %s \n", nxpath);
            fsFsClose(&fs);
            continue;
        }

        u64 bytes_read;
        rc = fsFileRead(&file, 0, name->str, sizeof(name->str), 0, &bytes_read);
        fsFileClose(&file);
        fsFsClose(&fs);

        if (R_FAILED(rc) || bytes_read != sizeof(name->str)) {
            // printf("failed to read file: %s \n", nxpath);
            continue;
        }

        return rc;
    }

    return rc;
}

void vfs_nx_init(bool enable_devices, bool save_writable) {
    g_enabled_devices = enable_devices;
    if (g_enabled_devices) {
        for (int i = 0; i < 2; i++) {
            ncmOpenContentStorage(&g_cs[i], NcmStorageId_SdCard - i);
            ncmOpenContentMetaDatabase(&g_db[i], NcmStorageId_SdCard - i);
        }

        // ftp_vfs_ns_init();
        ftp_vfs_ncm_init();
        // ftp_vfs_gc_init();
        ftp_vfs_save_init(save_writable);
    }
}

void vfs_nx_exit(void) {
    if (g_enabled_devices) {
        // ftp_vfs_ns_exit();
        ftp_vfs_ncm_exit();
        // ftp_vfs_gc_exit();
        ftp_vfs_save_exit();

        for (int i = 0; i < 2; i++) {
            ncmContentStorageClose(&g_cs[i]);
            ncmContentMetaDatabaseClose(&g_db[i]);
        }

        g_enabled_devices = false;
    }
}
