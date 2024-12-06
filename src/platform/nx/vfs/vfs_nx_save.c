/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */

#include "ftpsrv_vfs.h"
#include "../utils.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct SaveAcc {
    AccountUid uid;
    char name[0x20];
};

// hos only allows save fs to be mounted once...
// to work around this, we keep a cache of 16 saves (plenty)
struct SaveCacheEntry {
    FsFileSystem fs;
    u64 app_id;
    AccountUid uid;
    FsSaveDataType type;
    u32 ref_count;
};

static struct SaveCacheEntry g_save_cache[16];
static struct SaveAcc g_acc_profile[10];
static s32 g_acc_count;
static bool g_writable;

static FsFileSystem* mount_save_fs(const struct SavePathData* d) {
    for (int i = 0; i < 16; i++) {
        struct SaveCacheEntry* entry = &g_save_cache[i];
        if (entry->ref_count && entry->app_id == d->app_id && entry->type == d->data_type && !memcmp(&entry->uid, &d->uid, sizeof(d->uid))) {
            entry->ref_count++;
            return &entry->fs;
        }
    }

    // save is not currently mounted, find the next free slot
    for (int i = 0; i < 16; i++) {
        struct SaveCacheEntry* entry = &g_save_cache[i];
        if (!entry->ref_count) {
            FsSaveDataAttribute attr = {0};
            attr.application_id = d->app_id;
            attr.uid = d->uid;
            attr.save_data_type = d->data_type;

            Result rc;
            if (g_writable) {
                rc = fsOpenSaveDataFileSystem(&entry->fs, FsSaveDataSpaceId_User, &attr);
            } else {
                rc = fsOpenReadOnlySaveDataFileSystem(&entry->fs, FsSaveDataSpaceId_User, &attr);
            }

            if (R_FAILED(rc)) {
                // printf("\tfailed to open save: 0x%X id: %016lX\n", rc, d->app_id);
                return NULL;
            }

            entry->uid = attr.uid;
            entry->app_id = attr.application_id;
            entry->type = attr.save_data_type;
            entry->ref_count++;
            return &entry->fs;
        }
    }

    return NULL;
}

static void unmount_save_fs(const struct SavePathData* d) {
    for (int i = 0; i < 16; i++) {
        struct SaveCacheEntry* entry = &g_save_cache[i];
        if (entry->ref_count && entry->app_id == d->app_id && entry->type == d->data_type && !memcmp(&entry->uid, &d->uid, sizeof(d->uid))) {
            entry->ref_count--;
            if (!entry->ref_count) {
                if (g_writable) {
                    fsFsCommit(&entry->fs);
                }
                fsFsClose(&entry->fs);
            }
        }
    }
}

static struct SavePathData get_type(const char* path) {
    struct SavePathData data = {0};

    if (!strcmp(path, "save:")) {
        data.type = SaveDirType_Root;
    } else {
        const char* dilem = strchr(path, '[');
        if (!strncmp(path, "save:/bcat", strlen("save:/bcat"))) {
            data.data_type = FsSaveDataType_Bcat;
            data.type = SaveDirType_User;
        } else if (!strncmp(path, "save:/cache", strlen("save:/cache"))) {
            data.data_type = FsSaveDataType_Cache;
            data.type = SaveDirType_User;
        } else if (dilem && strlen(dilem) >= 33) {
            dilem++;
            char uid_buf[2][17];
            snprintf(uid_buf[0], sizeof(uid_buf[0]), "%s", dilem);
            snprintf(uid_buf[1], sizeof(uid_buf[1]), "%s", dilem + 0x10);

            data.uid.uid[0] = strtoull(uid_buf[0], NULL, 0x10);
            data.uid.uid[1] = strtoull(uid_buf[1], NULL, 0x10);

            data.data_type = FsSaveDataType_Account;
            data.type = SaveDirType_User;
            dilem = strchr(dilem, '[');
        }

        if (data.type && dilem && strlen(dilem) >= 17) {
            dilem++;
            data.app_id = strtoull(dilem, NULL, 0x10);
            data.type = SaveDirType_App;
            dilem += 17;
            data.path_off = dilem - path;
        }
    }

    return data;
}

static void build_native_path(char out[static FS_MAX_PATH], const char* path, const struct SavePathData* data) {
    if (strlen(path + data->path_off)) {
        snprintf(out, FS_MAX_PATH, "%s", path + data->path_off);
    } else {
        strcpy(out, "/");
    }
}

int ftp_vfs_save_open(struct FtpVfsSaveFile* f, const char* path, enum FtpVfsOpenMode mode) {
    f->data = get_type(path);
    if (f->data.type != SaveDirType_App) {
        return -1;
    }

    FsFileSystem* fs = mount_save_fs(&f->data);
    if (!fs) {
        return -1;
    }

    char nxpath[FS_MAX_PATH];
    build_native_path(nxpath, path, &f->data);
    if (vfs_fs_internal_open(fs, &f->fs_file, nxpath, mode)) {
        unmount_save_fs(&f->data);
        return -1;
    }

    f->fs = *fs;
    f->is_valid = 1;
    return 0;
}

int ftp_vfs_save_read(struct FtpVfsSaveFile* f, void* buf, size_t size) {
    return vfs_fs_internal_read(&f->fs_file, buf, size);
}

int ftp_vfs_save_write(struct FtpVfsSaveFile* f, const void* buf, size_t size) {
    return vfs_fs_internal_write(&f->fs_file, buf, size);
}

int ftp_vfs_save_seek(struct FtpVfsSaveFile* f, size_t off) {
    return vfs_fs_internal_seek(&f->fs_file, off);
}

int ftp_vfs_save_fstat(struct FtpVfsSaveFile* f, const char* path, struct stat* st) {
    char nxpath[FS_MAX_PATH];
    build_native_path(nxpath, path, &f->data);
    return vfs_fs_internal_fstat(&f->fs, &f->fs_file, nxpath, st);
}

int ftp_vfs_save_close(struct FtpVfsSaveFile* f) {
    if (!ftp_vfs_save_isfile_open(f)) {
        return -1;
    }
    vfs_fs_internal_close(&f->fs_file);
    unmount_save_fs(&f->data);
    f->is_valid = 0;
    return 0;
}

int ftp_vfs_save_isfile_open(struct FtpVfsSaveFile* f) {
    return f->is_valid;
}

int ftp_vfs_save_opendir(struct FtpVfsSaveDir* f, const char* path) {
    f->data = get_type(path);
    if (f->data.type == SaveDirType_Invalid) {
        return -1;
    } else if (f->data.type == SaveDirType_User) {
        FsSaveDataFilter filter = {0};
        filter.filter_by_save_data_type = true;
        filter.attr.save_data_type = f->data.data_type;

        if (f->data.data_type == FsSaveDataType_Account) {
            filter.filter_by_user_id = true;
            filter.attr.uid = f->data.uid;
        }

        Result rc;
        if (R_FAILED(rc = fsOpenSaveDataInfoReaderWithFilter(&f->r, FsSaveDataSpaceId_User, &filter))) {
            // printf("\tfailed to open filter: 0x%X %016lX%016lX vs %016lX%016lX\n", rc, p->uid.uid[0], p->uid.uid[1], f->data.uid.uid[0], f->data.uid.uid[1]);
            return -1;
        }
    } else if (f->data.type == SaveDirType_App) {
        FsFileSystem* fs = mount_save_fs(&f->data);
        if (!fs) {
            return -1;
        }
        f->fs = *fs;

        char nxpath[FS_MAX_PATH] = {"/"};
        build_native_path(nxpath, path, &f->data);
        if (vfs_fs_internal_opendir(&f->fs, &f->fs_dir, nxpath)) {
            unmount_save_fs(&f->data);
            return -1;
        }
    }

    f->index = 0;
    f->is_valid = 1;
    return 0;
}

const char* ftp_vfs_save_readdir(struct FtpVfsSaveDir* f, struct FtpVfsSaveDirEntry* entry) {
    Result rc;
    switch (f->data.type) {
        default: case SaveDirType_Invalid:
            return NULL;

        case SaveDirType_Root: {
            if (f->index >= g_acc_count) {
                return NULL;
            }
            const struct SaveAcc* p = &g_acc_profile[f->index];
            if (!accountUidIsValid(&p->uid)) {
                snprintf(entry->name, sizeof(entry->name), "%s", p->name);
            } else {
                snprintf(entry->name, sizeof(entry->name), "%s [%016lX%016lX]", p->name, p->uid.uid[0], p->uid.uid[1]);
            }
            f->index++;
            return entry->name;
        }

        case SaveDirType_User: {
            s64 total;
            if (R_FAILED(rc = fsSaveDataInfoReaderRead(&f->r, &entry->info, 1, &total))) {
                return NULL;
            }

            if (total <= 0) {
                return NULL;
            }

            // this can fail if the game is no longer installed.
            NcmContentId id;
            struct AppName name;
            if (R_FAILED(rc = get_app_name(entry->info.application_id, &id, &name))) {
                snprintf(entry->name, sizeof(entry->name), "[%016lX]", entry->info.application_id);
            } else {
                snprintf(entry->name, sizeof(entry->name), "%s [%016lX]", name.str, entry->info.application_id);
            }
            return entry->name;
        }

        case SaveDirType_App: {
            return vfs_fs_internal_readdir(&f->fs_dir, &entry->fs_buf);
        }
    }
}

int ftp_vfs_save_dirstat(struct FtpVfsSaveDir* f, const struct FtpVfsSaveDirEntry* entry, const char* path, struct stat* st) {
    if (f->data.type == SaveDirType_App) {
        char nxpath[FS_MAX_PATH];
        build_native_path(nxpath, path, &f->data);
        return vfs_fs_internal_dirstat(&f->fs, &f->fs_dir, &entry->fs_buf, nxpath, st);
    } else {
        memset(st, 0, sizeof(*st));
        st->st_nlink = 1;
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    }
    return 0;
}

int ftp_vfs_save_dirlstat(struct FtpVfsSaveDir* f, const struct FtpVfsSaveDirEntry* entry, const char* path, struct stat* st) {
    return ftp_vfs_save_dirstat(f, entry, path, st);
}

int ftp_vfs_save_closedir(struct FtpVfsSaveDir* f) {
    if (!ftp_vfs_save_isdir_open(f)) {
        return -1;
    }

    if (f->data.type == SaveDirType_User) {
        fsSaveDataInfoReaderClose(&f->r);
    } else if (f->data.type == SaveDirType_App) {
        vfs_fs_internal_closedir(&f->fs_dir);
        unmount_save_fs(&f->data);
    }

    memset(f, 0, sizeof(*f));
    return 0;
}

int ftp_vfs_save_isdir_open(struct FtpVfsSaveDir* f) {
    return f->is_valid;
}

int ftp_vfs_save_stat(const char* path, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;

    const struct SavePathData data = get_type(path);
    if (data.type != SaveDirType_App) {
        memset(st, 0, sizeof(*st));
        st->st_nlink = 1;
        st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    } else {
        FsFileSystem* fs = mount_save_fs(&data);
        if (!fs) {
            return -1;
        }

        char nxpath[FS_MAX_PATH];
        build_native_path(nxpath, path, &data);
        int rc = vfs_fs_internal_stat(fs, nxpath, st);
        unmount_save_fs(&data);
        return rc;
    }

    return 0;
}

int ftp_vfs_save_lstat(const char* path, struct stat* st) {
    return ftp_vfs_save_stat(path, st);
}

int ftp_vfs_save_mkdir(const char* path) {
    const struct SavePathData data = get_type(path);
    if (data.type != SaveDirType_App) {
        return -1;
    }

    FsFileSystem* fs = mount_save_fs(&data);
    if (!fs) {
        return -1;
    }

    char nxpath[FS_MAX_PATH];
    build_native_path(nxpath, path, &data);
    int rc = vfs_fs_internal_mkdir(fs, nxpath);
    unmount_save_fs(&data);
    return rc;
}

int ftp_vfs_save_unlink(const char* path) {
    const struct SavePathData data = get_type(path);
    if (data.type != SaveDirType_App) {
        return -1;
    }

    FsFileSystem* fs = mount_save_fs(&data);
    if (!fs) {
        return -1;
    }

    char nxpath[FS_MAX_PATH];
    build_native_path(nxpath, path, &data);
    int rc = vfs_fs_internal_unlink(fs, nxpath);
    unmount_save_fs(&data);
    return rc;
}

int ftp_vfs_save_rmdir(const char* path) {
    const struct SavePathData data = get_type(path);
    if (data.type != SaveDirType_App) {
        return -1;
    }

    FsFileSystem* fs = mount_save_fs(&data);
    if (!fs) {
        return -1;
    }

    char nxpath[FS_MAX_PATH];
    build_native_path(nxpath, path, &data);
    int rc = vfs_fs_internal_rmdir(fs, nxpath);
    unmount_save_fs(&data);
    return rc;
}

int ftp_vfs_save_rename(const char* src, const char* dst) {
    const struct SavePathData data_src = get_type(src);
    const struct SavePathData data_dst = get_type(dst);
    if (data_src.type != SaveDirType_App) {
        return -1;
    }

    if (data_src.app_id != data_dst.app_id || memcmp(&data_src.uid, &data_dst.uid, sizeof(data_src.uid))) {
        return -1;
    }

    FsFileSystem* fs = mount_save_fs(&data_src);
    if (!fs) {
        return -1;
    }

    char nxpath_src[FS_MAX_PATH];
    char nxpath_dst[FS_MAX_PATH];
    build_native_path(nxpath_src, src, &data_src);
    build_native_path(nxpath_dst, dst, &data_dst);
    int rc = vfs_fs_internal_rename(fs, nxpath_src, nxpath_dst);
    unmount_save_fs(&data_src);
    return rc;
}

void ftp_vfs_save_init(bool save_writable) {
    g_writable = save_writable;

    AccountUid uids[8];
    s32 count;
    if (R_SUCCEEDED(accountListAllUsers(uids, 8, &count))) {
        for (int i = 0; i < count; i++) {
            AccountProfile profile;
            if (R_SUCCEEDED(accountGetProfile(&profile, uids[i]))) {
                AccountProfileBase base;
                if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base))) {
                    strcpy(g_acc_profile[g_acc_count].name, base.nickname);
                    g_acc_profile[g_acc_count].uid = base.uid;
                    g_acc_count++;
                }
                accountProfileClose(&profile);
            }
        }

        strcpy(g_acc_profile[g_acc_count++].name, "bcat");
        // doesn't work?
        // strcpy(g_acc_profile[g_acc_count++].name, "cache");
    }
}

void ftp_vfs_save_exit(void) {
    for (int i = 0; i < 16; i++) {
        struct SaveCacheEntry* entry = &g_save_cache[i];
        if (entry->ref_count) {
            if (g_writable) {
                fsFsCommit(&entry->fs);
            }
            fsFsClose(&entry->fs);
        }
    }
}
