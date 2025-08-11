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
#include <stdlib.h>
#include <minIni.h>
#include <ctype.h>
#include <switch.h>

#define NCM_SIZE 2
#define DEVICE_NUM 32
#define QLAUNCH_TID 0x0100000000001000ULL

static const char* INI_PATH = "/switch/breeze/config.ini";
static bool g_enabled_devices = false;
static NcmContentStorage g_cs[NCM_SIZE];
static NcmContentMetaDatabase g_db[NCM_SIZE];
static struct VfsDeviceEntry g_device[DEVICE_NUM];
static enum VFS_TYPE g_device_type[DEVICE_NUM];
static u32 g_device_count;
static char g_today_path[16];

static const FtpVfs* g_vfs[] = {
    [VFS_TYPE_NONE] = &g_vfs_none,
    [VFS_TYPE_ROOT] = &g_vfs_root,
    [VFS_TYPE_FS] = &g_vfs_fs,
#if USE_VFS_SAVE
    [VFS_TYPE_SAVE] = &g_vfs_save,
#endif
#if USE_VFS_STORAGE
    [VFS_TYPE_STORAGE] = &g_vfs_storage,
#endif
#if USE_VFS_GC
    [VFS_TYPE_GC] = &g_vfs_gc,
#endif
#if USE_VFS_USBHSFS
    [VFS_TYPE_STDIO] = &g_vfs_stdio,
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

int ftp_vfs_seek(struct FtpVfsFile* f, const void* buf, size_t size, size_t off) {
    return g_vfs[f->type]->seek(&f->root, buf, size, off);
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

int ftp_vfs_dirlstat(struct FtpVfsDir* f, const struct FtpVfsDirEntry* entry, const char* path, struct stat* st) {
    return g_vfs[f->type]->dirlstat(&f->root, &entry->root, fix_path(path, f->type), st);
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

Result get_app_name2(u64 app_id, NcmContentMetaDatabase* db, NcmContentStorage* cs, NcmContentId* id, struct AppName* name) {
    Result rc;
    NcmContentMetaKey key;
    s32 entries_total;
    s32 entries_written;
    if (R_FAILED(rc = ncmContentMetaDatabaseList(db, &entries_total, &entries_written, &key, 1, NcmContentMetaType_Application, app_id, 0, UINT64_MAX, NcmContentInstallType_Full))) {
        return rc;
    }

    if (R_FAILED(rc = ncmContentMetaDatabaseGetContentIdByType(db, id, &key, NcmContentType_Control))) {
        return rc;
    }

    char nxpath[FS_MAX_PATH];
    if (R_FAILED(rc = ncmContentStorageGetPath(cs, nxpath, sizeof(nxpath), id))) {
        return rc;
    }

    FsFileSystem fs;
    if (R_FAILED(rc = fsOpenFileSystemWithId(&fs, key.id, FsFileSystemType_ContentControl, nxpath, FsContentAttributes_All))) {
        return rc;
    }

    strcpy(nxpath, "/control.nacp");
    FsFile file;
    if (R_FAILED(rc = fsFsOpenFile(&fs, nxpath, FsOpenMode_Read, &file))) {
        fsFsClose(&fs);
        return rc;
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
    return rc;
}

Result get_app_name(u64 app_id, NcmContentId* id, struct AppName* name) {
    Result rc;

    for (int i = 0; i < NCM_SIZE; i++) {
        if (R_SUCCEEDED(rc = get_app_name2(app_id, &g_db[i], &g_cs[i], id, name))) {
            return rc;
        }
    }

    return rc;
}

struct MountEntry {
    const char* name;
    FsBisPartitionId id;
};

static const struct MountEntry BIS_NAMES[] = {
    { "bis_calibration_file", FsBisPartitionId_CalibrationFile },
    { "bis_safe_mode", FsBisPartitionId_SafeMode },
    { "bis_user", FsBisPartitionId_User },
    { "bis_system", FsBisPartitionId_System },
};
void vfs_nx_update_mounts(void) {
    if (!g_enabled_devices) {
        return;
    }

    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    static char current_day_path[16];
    sprintf(current_day_path, "/%04d/%02d/%02d", local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday);

    if (strcmp(g_today_path, current_day_path) != 0) {
        // Day has changed, remount the directory
        fsdev_wrapUnmountDevice("album_sd_today");

        FsFileSystem* album_sd = fsdev_wrapGetDeviceFileSystem("album_sd");
        if (album_sd) {
            strcpy(g_today_path, current_day_path);
            fsdev_wrapMountDevice("album_sd_today", g_today_path, *album_sd, false);
        }
    }
}

void vfs_nx_update_config_mounts(void) {
    static u64 last_tid = 0;
    static char ams_tid_mount_str[128] = {0};
    static char breeze_tid_mount_str[128] = {0};
    static char breeze_name_mount_str[128] = {0};
    static char name_32[32];

    u64 pid, current_tid = 0;
    Result rc;
    if (R_SUCCEEDED(pmdmntInitialize())) {
        if (R_SUCCEEDED(pminfoInitialize())) {
            if (R_SUCCEEDED(rc = pmdmntGetApplicationProcessId(&pid))) {
                if (0x20f == pminfoGetProgramId(&current_tid, pid)) {
                    current_tid = QLAUNCH_TID;
                }
            } else if (rc == 0x20f) {
                current_tid = QLAUNCH_TID;
            } else {
                current_tid = 0;
            }
            pminfoExit();
        }
        pmdmntExit();
    }

    if (last_tid != current_tid) {
        fsdev_wrapUnmountDevice("ams_tid_mount");
        vfs_nx_remove_device("ams_tid_mount");
        fsdev_wrapUnmountDevice("breeze_tid_mount");
        vfs_nx_remove_device("breeze_tid_mount");
        fsdev_wrapUnmountDevice(name_32);
        vfs_nx_remove_device(name_32);

        last_tid = current_tid;

        if (current_tid != 0 && current_tid != QLAUNCH_TID) {
            struct AppName name;
            NcmContentId id;
            if (R_SUCCEEDED(get_app_name(current_tid, &id, &name))) {
                char mount_path[FS_MAX_PATH];
                FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");

                if (sdmc) {
                    snprintf(ams_tid_mount_str, sizeof(ams_tid_mount_str), "/atmosphere/contents/%016lx", current_tid);
                    log_file_fwrite("nx: ams_tid_mount=%s", ams_tid_mount_str);
                    if (!fsdev_wrapMountDevice("ams_tid_mount", ams_tid_mount_str, *sdmc, false)) {
                        vfs_nx_add_device("ams_tid_mount", VFS_TYPE_FS);
                    }

                    snprintf(breeze_tid_mount_str, sizeof(breeze_tid_mount_str), "/switch/breeze/cheats/%016lx", current_tid);
                    log_file_fwrite("nx: breeze_tid_mount=%s", breeze_tid_mount_str);
                    if (!fsdev_wrapMountDevice("breeze_tid_mount", breeze_tid_mount_str, *sdmc, false)) {
                        vfs_nx_add_device("breeze_tid_mount", VFS_TYPE_FS);
                    }
                    
                    sanitize_fs_name(name.str);
                    strncpy(name_32, name.str, 31);
                    name_32[31] = '\0';
                    snprintf(breeze_name_mount_str, sizeof(breeze_name_mount_str), "/switch/breeze/cheats/%s", name.str);
                    log_file_fwrite("nx: %s=%s", name_32, breeze_name_mount_str);
                    if (!fsdev_wrapMountDevice(name_32, breeze_name_mount_str, *sdmc, false)) {
                        vfs_nx_add_device(name_32, VFS_TYPE_FS);
                    }
                }
            }
        }
    }
    if (current_tid != QLAUNCH_TID && current_tid != 0) return;
    static char game_cheat_dir_str[128] = {0};
    char new_game_cheat_dir_str[128] = {0};
    char new_game_cheat_dir_str_tmp[128] = {0};

    ini_gets("Nx", "game_cheat_dir", "", new_game_cheat_dir_str_tmp, sizeof(new_game_cheat_dir_str_tmp), INI_PATH);

    snprintf(new_game_cheat_dir_str, sizeof(new_game_cheat_dir_str), "/switch/breeze/cheats/%s", new_game_cheat_dir_str_tmp);

    if (strcmp(game_cheat_dir_str, new_game_cheat_dir_str) != 0) {
        log_file_fwrite("nx: game_cheat_dir=%s", new_game_cheat_dir_str);
        // log_file_write("nx: game_cheat_dir changed; updating mount");
        if (game_cheat_dir_str[0] != '\0') {
            fsdev_wrapUnmountDevice("breeze_cheat_dir");
            vfs_nx_remove_device("breeze_cheat_dir");
        }
        strcpy(game_cheat_dir_str, new_game_cheat_dir_str);
        if (game_cheat_dir_str[0] != '\0') {    
            FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
            if (sdmc) {
                if (!fsdev_wrapMountDevice("breeze_cheat_dir", game_cheat_dir_str, *sdmc, false)) {
                    vfs_nx_add_device("breeze_cheat_dir", VFS_TYPE_FS);
                }
            }
        }
    }

    static char atm_game_dir_path[160] = {0};
    char new_atm_game_dir_path[160] = {0};
    char save_id_str[21] = {0};
    ini_gets("Nx", "save_application_id", "0", save_id_str, sizeof(save_id_str), INI_PATH);
    u64 save_id = strtoull(save_id_str, NULL, 16);

    if (save_id != 0) {
        sprintf(new_atm_game_dir_path, "/atmosphere/contents/%016lx/cheats", (unsigned long)save_id);
    }
    if (strcmp(atm_game_dir_path, new_atm_game_dir_path) != 0) {
        log_file_fwrite("nx: atmosphere_cheat_dir=%s", new_atm_game_dir_path);
        // log_file_write("nx: atmosphere_cheat_dir changed; updating mount");
        if (atm_game_dir_path[0] != '\0') {
            fsdev_wrapUnmountDevice("atmosphere_cheat_dir");
            vfs_nx_remove_device("atmosphere_cheat_dir");
        }
        strcpy(atm_game_dir_path, new_atm_game_dir_path);
        if (atm_game_dir_path[0] != '\0') {
            FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
            if (sdmc) {
                if (!fsdev_wrapMountDevice("atmosphere_cheat_dir", atm_game_dir_path, *sdmc, false)) {
                    vfs_nx_add_device("atmosphere_cheat_dir", VFS_TYPE_FS);
                }
            }
        }
    }
}


void vfs_nx_init(bool enable_devices, bool save_writable, bool mount_bis, bool mount_save) {
    g_enabled_devices = enable_devices;
    if (g_enabled_devices) {

        const bool mount_misc = ini_getbool("Nx", "mount_misc", 0, INI_PATH);
        if (!mount_misc) ini_puts("Nx", "mount_misc", "0", INI_PATH);
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
        if (mount_misc){
            if (!fsdev_wrapMountImage("album_nand", FsImageDirectoryId_Nand)) {
                vfs_nx_add_device("album_nand", VFS_TYPE_FS);
            }
        }
        if (!fsdev_wrapMountImage("album_sd", FsImageDirectoryId_Sd)) {
            vfs_nx_add_device("album_sd", VFS_TYPE_FS);
        }
        FsFileSystem* album_sd = fsdev_wrapGetDeviceFileSystem("album_sd");
        if (album_sd) {
#include <time.h>
            time_t now = time(NULL);
            struct tm *local_time = localtime(&now);
            static char today[16];
            sprintf(g_today_path,"/%04d/%02d/%02d", local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday);
            if (!fsdev_wrapMountDevice("album_sd_today", g_today_path, *album_sd, false)) {
                vfs_nx_add_device("album_sd_today", VFS_TYPE_FS);
            }
        }
        // bis storage
        if (mount_misc) {
        if (mount_bis) {
#if USE_VFS_STORAGE
        vfs_storage_init();
        vfs_nx_add_device("bis", VFS_TYPE_STORAGE);
#endif
        // bis fs
            for (int i = 0; i < ARRAY_SIZE(BIS_NAMES); i++) {
                if (!fsdev_wrapMountBis(BIS_NAMES[i].name, BIS_NAMES[i].id)) {
                    vfs_nx_add_device(BIS_NAMES[i].name, VFS_TYPE_FS);
                }
            }
        }

        // content storage
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenContentStorageFileSystem(&fs, FsContentStorageId_System))) {
            fsdev_wrapMountDevice("content_system", NULL, fs, true);
            vfs_nx_add_device("content_system", VFS_TYPE_FS);
        }
        if (R_SUCCEEDED(fsOpenContentStorageFileSystem(&fs, FsContentStorageId_User))) {
            fsdev_wrapMountDevice("content_user", NULL, fs, true);
            vfs_nx_add_device("content_user", VFS_TYPE_FS);
        }
        if (R_SUCCEEDED(fsOpenContentStorageFileSystem(&fs, FsContentStorageId_SdCard))) {
            fsdev_wrapMountDevice("content_sdcard", NULL, fs, true);
            vfs_nx_add_device("content_sdcard", VFS_TYPE_FS);
        }
        if (R_SUCCEEDED(fsOpenContentStorageFileSystem(&fs, FsContentStorageId_System0))) {
            fsdev_wrapMountDevice("content_system0", NULL, fs, true);
            vfs_nx_add_device("content_system0", VFS_TYPE_FS);
        }

        // custom storage
        if (R_SUCCEEDED(fsOpenCustomStorageFileSystem(&fs, FsCustomStorageId_System))) {
            fsdev_wrapMountDevice("custom_system", NULL, fs, true);
            vfs_nx_add_device("custom_system", VFS_TYPE_FS);
        }
        if (R_SUCCEEDED(fsOpenCustomStorageFileSystem(&fs, FsCustomStorageId_SdCard))) {
            fsdev_wrapMountDevice("custom_sd", NULL, fs, true);
            vfs_nx_add_device("custom_sd", VFS_TYPE_FS);
        }
        }

        // add some shortcuts.
        FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
        if (sdmc) {
            if (mount_misc) {
                if (!fsdev_wrapMountDevice("switch", "/switch", *sdmc, false)) {
                    vfs_nx_add_device("switch", VFS_TYPE_FS);
                }
                if (!fsdev_wrapMountDevice("switch", "/switch", *sdmc, false)) {
                    vfs_nx_add_device("switch", VFS_TYPE_FS);
                }
            }
            if (!fsdev_wrapMountDevice("breeze", "/switch/breeze", *sdmc, false)) {
                vfs_nx_add_device("breeze", VFS_TYPE_FS);
            }

            char save_id_str[21] = {0};
            ini_gets("Nx", "save_application_id", "", save_id_str, sizeof(save_id_str), INI_PATH);

            char bcat_id_str[21] = {0};
            ini_gets("Nx", "bcat_application_id", "", bcat_id_str, sizeof(bcat_id_str), INI_PATH);

            const u64 save_id = strtoull(save_id_str, NULL, 16);
            const u64 bcat_id = strtoull(bcat_id_str, NULL, 16);
            AccountUid uid[2];
            s32 actual_total;
            if (mount_save) {
                if (R_SUCCEEDED(accountListAllUsers(uid, 2, &actual_total))) {
                    if (R_SUCCEEDED(fsdev_wrapMountSave("save0", save_id, uid[0]))) {
                        vfs_nx_add_device("save0", VFS_TYPE_FS);
                    }
                    if (R_SUCCEEDED(fsdev_wrapMountSave("save1", save_id, uid[1]))) {
                        vfs_nx_add_device("save1", VFS_TYPE_FS);
                    }
                }
                if (R_SUCCEEDED(fsdev_wrapMountSaveBcat("bcat", bcat_id))) {
                    vfs_nx_add_device("bcat", VFS_TYPE_FS);
                }
            }
        }
        if (mount_misc) {
#if USE_VFS_GC
        if (R_SUCCEEDED(vfs_gc_init())) {
            vfs_nx_add_device("gc", VFS_TYPE_GC);
        }
#endif
#if USE_VFS_SAVE
        vfs_save_init(save_writable);
        vfs_nx_add_device("save", VFS_TYPE_SAVE);
        }
#endif

#if USE_VFS_USBHSFS
        if (mount_misc) {
        if (R_SUCCEEDED(romfsMountFromCurrentProcess("romfs"))) {
            vfs_nx_add_device("romfs", VFS_TYPE_STDIO);
        }

        if (R_SUCCEEDED(romfsMountDataStorageFromProgram(0x0100000000001000, "romfs_qlaunch"))) {
            vfs_nx_add_device("romfs_qlaunch", VFS_TYPE_STDIO);
        }

        if (R_SUCCEEDED(vfs_hdd_init())) {
            vfs_nx_add_device("hdd", VFS_TYPE_HDD);
        }
        }
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

    // Ensure config-driven mounts are set even when mount_devices is disabled
    vfs_nx_update_config_mounts();
}

void vfs_nx_exit(void) {
    if (g_enabled_devices) {
#if USE_VFS_GC
        vfs_gc_exit();
#endif
#if USE_VFS_STORAGE
        vfs_storage_exit();
#endif
#if USE_VFS_SAVE
        vfs_save_exit();
#endif
        vfs_root_exit();
#if USE_VFS_USBHSFS
        romfsUnmount("romfs_qlaunch");
        romfsUnmount("romfs");
        vfs_hdd_exit();
#endif

        for (int i = 0; i < NCM_SIZE; i++) {
            ncmContentStorageClose(&g_cs[i]);
            ncmContentMetaDatabaseClose(&g_db[i]);
        }

        g_enabled_devices = false;
    }
}

void vfs_nx_add_device(const char* name, enum VFS_TYPE type) {
    if (g_device_count >= DEVICE_NUM) {
        return;
    }

    if (strlen(name) >= sizeof(g_device[0].name) + 2) {
        return;
    }

    snprintf(g_device[g_device_count].name, sizeof(g_device[g_device_count].name), "%s:", name);
    g_device_type[g_device_count] = type;
    g_device_count++;
}

void vfs_nx_remove_device(const char* name) {
    char full_name[32];
    snprintf(full_name, sizeof(full_name), "%s:", name);

    for (u32 i = 0; i < g_device_count; i++) {
        if (strcmp(g_device[i].name, full_name) == 0) {
            for (u32 j = i; j < g_device_count - 1; j++) {
                g_device[j] = g_device[j + 1];
                g_device_type[j] = g_device_type[j + 1];
            }
            g_device_count--;
            break;
        }
    }
}
