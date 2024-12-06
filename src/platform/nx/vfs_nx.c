/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include "log/log.h"
#include "utils.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define NCM_SIZE 2
#define DEVICE_NUM 32

static bool g_enabled_devices = false;
static NcmContentStorage g_cs[NCM_SIZE];
static NcmContentMetaDatabase g_db[NCM_SIZE];
static struct VfsDeviceEntry g_device[DEVICE_NUM];
static enum VFS_TYPE g_device_type[DEVICE_NUM];
static u32 g_device_count;

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

static int vfs_none_seek(void* user, size_t off) {
    return set_errno_and_return_minus1();
}

static int vfs_none_fstat(void* user, const char* path, struct stat* st) {
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

static int vfs_none_dirstat(void* user, const void* user_entry, const char* path, struct stat* st) {
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

static const FtpVfs g_vfs_none = {
    .open = vfs_none_open,
    .read = vfs_none_read,
    .write = vfs_none_write,
    .seek = vfs_none_seek,
    .fstat = vfs_none_fstat,
    .close = vfs_none_close,
    .isfile_open = vfs_none_isfile_open,
    .opendir = vfs_none_opendir,
    .readdir = vfs_none_readdir,
    .dirstat = vfs_none_dirstat,
    .dirlstat = vfs_none_dirstat,
    .closedir = vfs_none_closedir,
    .isdir_open = vfs_none_isdir_open,
    .stat = vfs_none_stat,
    .lstat = vfs_none_stat,
    .mkdir = vfs_none_mkdir,
    .unlink = vfs_none_unlink,
    .rmdir = vfs_none_rmdir,
    .rename = vfs_none_rename,
};

static const FtpVfs* g_vfs[] = {
    [VFS_TYPE_NONE] = &g_vfs_none,
    [VFS_TYPE_ROOT] = &g_vfs_root,
    [VFS_TYPE_FS] = &g_vfs_fs,
    [VFS_TYPE_SAVE] = &g_vfs_save,
#if USE_USBHSFS
    [VFS_TYPE_HDD] = &g_vfs_hdd,
#endif
};

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
            for (u32 i = 0; i < g_device_count; i++) {
                if (is_path(path, g_device[i].name)) {
                    return g_device_type[i];
                }
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

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    f->type = get_type(path);
    return g_vfs[f->type]->open(&f->root, fix_path(path, f->type), mode);
}

int ftp_vfs_read(struct FtpVfsFile* f, void* buf, size_t size) {
    return g_vfs[f->type]->read(&f->root, buf, size);
}

int ftp_vfs_write(struct FtpVfsFile* f, const void* buf, size_t size) {
    return g_vfs[f->type]->write(&f->root, buf, size);
}

int ftp_vfs_seek(struct FtpVfsFile* f, size_t off) {
    return g_vfs[f->type]->seek(&f->root, off);
}

int ftp_vfs_fstat(struct FtpVfsFile* f, const char* path, struct stat* st) {
    return g_vfs[f->type]->fstat(&f->root, fix_path(path, f->type), st);
}

int ftp_vfs_close(struct FtpVfsFile* f) {
    const enum VFS_TYPE type = f->type;
    f->type = VFS_TYPE_NONE;
    return g_vfs[type]->close(&f->root);
}

int ftp_vfs_isfile_open(struct FtpVfsFile* f) {
    return g_vfs[f->type]->isfile_open(&f->root);
}

int ftp_vfs_opendir(struct FtpVfsDir* f, const char* path) {
    f->type = get_type(path);
    return g_vfs[f->type]->opendir(&f->root, fix_path(path, f->type));
}

const char* ftp_vfs_readdir(struct FtpVfsDir* f, struct FtpVfsDirEntry* entry) {
    return g_vfs[f->type]->readdir(&f->root, &entry->root);
}

int ftp_vfs_dirstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return g_vfs[f->type]->dirstat(&f->root, &entry->root, fix_path(path, f->type), st);
}

int ftp_vfs_dirlstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return ftp_vfs_dirstat(f, entry, path, st);
}

int ftp_vfs_closedir(struct FtpVfsDir* f) {
    const enum VFS_TYPE type = f->type;
    f->type = VFS_TYPE_NONE;
    return g_vfs[type]->closedir(&f->root);
}

int ftp_vfs_isdir_open(struct FtpVfsDir* f) {
    return g_vfs[f->type]->isdir_open(&f->root);
}

int ftp_vfs_stat(const char* path, struct stat* st) {
    enum VFS_TYPE type = get_type(path);
    const char* dilem = strchr(path, ':');

    if (type != VFS_TYPE_NONE && dilem && (!strcmp(dilem, ":") || !strcmp(dilem, ":/"))) {
        return g_vfs[VFS_TYPE_ROOT]->stat(fix_path(path, type), st);
    }

    return g_vfs[type]->stat(fix_path(path, type), st);
}

int ftp_vfs_lstat(const char* path, struct stat* st) {
    return ftp_vfs_stat(path, st);
}

int ftp_vfs_mkdir(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    return g_vfs[type]->mkdir(fix_path(path, type));
}

int ftp_vfs_unlink(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    return g_vfs[type]->unlink(fix_path(path, type));
}

int ftp_vfs_rmdir(const char* path) {
    const enum VFS_TYPE type = get_type(path);
    return g_vfs[type]->rmdir(fix_path(path, type));
}

int ftp_vfs_rename(const char* src, const char* dst) {
    const enum VFS_TYPE src_type = get_type(src);
    const enum VFS_TYPE dst_type = get_type(dst);
    if (src_type != dst_type) {
        errno = EXDEV; // this will do for the error.
        return -1;
    }

    return g_vfs[src_type]->rename(fix_path(src, src_type), fix_path(dst, dst_type));
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

static const u32 g_nacpLanguageTable[15] = {
    [SetLanguage_JA]    = 2,
    [SetLanguage_ENUS]  = 0,
    [SetLanguage_ENGB]  = 1,
    [SetLanguage_FR]    = 3,
    [SetLanguage_DE]    = 4,
    [SetLanguage_ES419] = 5,
    [SetLanguage_ES]    = 6,
    [SetLanguage_IT]    = 7,
    [SetLanguage_NL]    = 8,
    [SetLanguage_FRCA]  = 9,
    [SetLanguage_PT]    = 10,
    [SetLanguage_RU]    = 11,
    [SetLanguage_KO]    = 12,
    [SetLanguage_ZHTW]  = 13,
    [SetLanguage_ZHCN]  = 14,
};

static u8 g_lang_index;

Result get_app_name(u64 app_id, NcmContentId* id, struct AppName* name) {
    Result rc;

    for (int i = 0; i < NCM_SIZE; i++) {
        NcmContentMetaKey key;
        s32 entries_total;
        s32 entries_written;
        if (R_FAILED(rc = ncmContentMetaDatabaseList(&g_db[i], &entries_total, &entries_written, &key, 1, NcmContentMetaType_Application, app_id, 0, UINT64_MAX, NcmContentInstallType_Full))) {
            continue;
        }

        if (R_FAILED(rc = ncmContentMetaDatabaseGetContentIdByType(&g_db[i], id, &key, NcmContentType_Control))) {
            continue;
        }

        char nxpath[FS_MAX_PATH] = {0};
        if (R_FAILED(rc = ncmContentStorageGetPath(&g_cs[i], nxpath, sizeof(nxpath), id))) {
            continue;
        }

        FsFileSystem fs;
        if (R_FAILED(rc = fsOpenFileSystemWithId(&fs, key.id, FsFileSystemType_ContentControl, nxpath, FsContentAttributes_All))) {
            continue;
        }

        strcpy(nxpath, "/control.nacp");
        FsFile file;
        if (R_FAILED(rc = fsFsOpenFile(&fs, nxpath, FsOpenMode_Read, &file))) {
            fsFsClose(&fs);
            continue;
        }

        name->str[0] = '\0';
        s64 off = g_lang_index * sizeof(NacpLanguageEntry);
        u64 bytes_read;
        rc = fsFileRead(&file, off, name->str, sizeof(name->str), 0, &bytes_read);
        if (name->str[0] == '\0') {
            for (int i = 0; i < 16; i++) {
                off = i * sizeof(NacpLanguageEntry);
                rc = fsFileRead(&file, off, name->str, sizeof(name->str), 0, &bytes_read);
                if (name->str[0] != '\0') {
                    break;
                }
            }
        }
        fsFileClose(&file);
        fsFsClose(&fs);

        if (R_FAILED(rc) || bytes_read != sizeof(name->str)) {
            continue;
        }

        return rc;
    }

    return rc;
}

void vfs_nx_init(bool enable_devices, bool save_writable, bool mount_bis) {
    g_enabled_devices = enable_devices;
    if (g_enabled_devices) {

        // sorted based on most common
        const NcmStorageId ids[NCM_SIZE] = {
            NcmStorageId_SdCard,
            NcmStorageId_BuiltInUser,
        };

        for (int i = 0; i < NCM_SIZE; i++) {
            Result rc;
            if (R_FAILED(rc = ncmOpenContentStorage(&g_cs[i], ids[i]))) {
                log_file_fwrite("failed: ncmOpenContentStorage() 0x%X\n", rc);
            }
            if (R_FAILED(rc = ncmOpenContentMetaDatabase(&g_db[i], ids[i]))) {
                log_file_fwrite("failed: ncmOpenContentMetaDatabase() 0x%X\n", rc);
            }
        }

        vfs_nx_add_device("sdmc", VFS_TYPE_FS);

        if (!fsdev_wrapMountImage("image_nand", FsImageDirectoryId_Nand)) {
            vfs_nx_add_device("image_nand", VFS_TYPE_FS);
        }
        if (!fsdev_wrapMountImage("image_sd", FsImageDirectoryId_Sd)) {
            vfs_nx_add_device("image_sd", VFS_TYPE_FS);
        }

        if (mount_bis) {
            if (!fsdev_wrapMountBis("bis_system", FsBisPartitionId_System)) {
                vfs_nx_add_device("bis_system", VFS_TYPE_FS);
            }
            if (!fsdev_wrapMountBis("bis_safe", FsBisPartitionId_SafeMode)) {
                vfs_nx_add_device("bis_safe", VFS_TYPE_FS);
            }
            if (!fsdev_wrapMountBis("bis_user", FsBisPartitionId_User)) {
                vfs_nx_add_device("bis_user", VFS_TYPE_FS);
            }
        }

        // add some shortcuts.
        FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
        if (sdmc) {
            if (!fsdev_wrapMountDevice("switch", "/switch", *sdmc, false)) {
                vfs_nx_add_device("switch", VFS_TYPE_FS);
            }
            if (!fsdev_wrapMountDevice("contents", "/atmosphere/contents", *sdmc, false)) {
                vfs_nx_add_device("contents", VFS_TYPE_FS);
            }
        }

        vfs_save_init(save_writable);
        vfs_nx_add_device("save", VFS_TYPE_SAVE);
#if USE_USBHSFS
        vfs_hdd_init();
        vfs_nx_add_device("hdd", VFS_TYPE_HDD);
#endif
        vfs_root_init(g_device, &g_device_count);

        u64 LanguageCode;
        SetLanguage Language;
        if (R_SUCCEEDED(setGetSystemLanguage(&LanguageCode))) {
            setMakeLanguage(LanguageCode, &Language);
        }

        if (Language < 0 || Language >= 15) {
            Language = SetLanguage_ENUS;
        }

        g_lang_index = g_nacpLanguageTable[Language];
    }
}

void vfs_nx_exit(void) {
    if (g_enabled_devices) {
        vfs_save_exit();
#if USE_USBHSFS
        vfs_hdd_exit();
#endif
        vfs_root_exit();

        for (int i = 0; i < NCM_SIZE; i++) {
            ncmContentStorageClose(&g_cs[i]);
            ncmContentMetaDatabaseClose(&g_db[i]);
        }

        g_enabled_devices = false;
    }
}

void vfs_nx_add_device(const char* name, enum VFS_TYPE type) {
    if (g_device_count >= 32) {
        return;
    }

    if (strlen(name) >= sizeof(g_device[0].name) + 2) {
        return;
    }

    snprintf(g_device[g_device_count].name, sizeof(g_device[g_device_count].name), "%s:", name);
    g_device_type[g_device_count] = type;
    g_device_count++;
}
