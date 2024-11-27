#include "utils.h"
#include <string.h>
#include <stdio.h>

#define FsDevWrap_DEVICES_MAX 32

#define FsDevWrap_Module 0x505

enum FsDevWrap_Error {
    FsDevWrap_Error_FailedToMount = MAKERESULT(FsDevWrap_Module, 0x1),
};

struct FsDevWrapEntry {
    FsFileSystem fs;
    char path[32];
    const char* shortcut;
    bool active;
    bool own;
};

static struct FsDevWrapEntry g_fsdev_entries[FsDevWrap_DEVICES_MAX] = {0};

static Result mount_helper(const char* path, FsFileSystem fs) {
    if (fsdev_wrapMountDevice(path, NULL, fs, true)) {
        fsFsClose(&fs);
        return FsDevWrap_Error_FailedToMount;
    }
    return 0;
}

Result fsdev_wrapMountSdmc(void) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpenSdCardFileSystem(&fs))) {
        rc = mount_helper("sdmc", fs);
    }
    return rc;
}

Result fsdev_wrapMountImage(const char* path, FsImageDirectoryId id) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpenImageDirectoryFileSystem(&fs, id))) {
        rc = mount_helper(path, fs);
    }
    return rc;
}

Result fsdev_wrapMountContent(const char* path, FsContentStorageId id) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpenContentStorageFileSystem(&fs, id))) {
        rc = mount_helper(path, fs);
    }
    return rc;
}

Result fsdev_wrapMountBis(const char* path, FsBisPartitionId id) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpenBisFileSystem(&fs, id, ""))) {
        rc = mount_helper(path, fs);
    }
    return rc;
}

Result fsdev_wrapMountSave(const char* path, u64 id, AccountUid uid) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpen_SaveData(&fs, id, uid))) {
        rc = mount_helper(path, fs);
    }
    return rc;
}

Result fsdev_wrapMountSaveBcat(const char* path, u64 id) {
    FsFileSystem fs;
    Result rc;
    if (R_SUCCEEDED(rc = fsOpen_BcatSaveData(&fs, id))) {
        rc = mount_helper(path, fs);
    }
    return rc;
}

FsFileSystem* fsdev_wrapGetDeviceFileSystem(const char* name) {
    size_t len = strlen(name);
    if (!len) {
        return NULL;
    }

    const char* sdmc_path = "sdmc";
    if (name[0] == '/') {
        name = sdmc_path;
    }

    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (g_fsdev_entries[i].active && !strncmp(name, g_fsdev_entries[i].path, strlen(name))) {
            return &g_fsdev_entries[i].fs;
        }
    }
    return NULL;
}

int fsdev_wrapTranslatePath(const char *path, FsFileSystem** device, char *outpath) {
    size_t len = strlen(path);
    if (!len) {
        return -1;
    }

    const char* sdmc_path = "sdmc:/";
    if (path[0] == '/') {
        path = sdmc_path;
    }

    const char* colon = memchr(path, ':', len);
    if (!colon || colon == path) {
        return -1;
    }

    const size_t device_name_len = colon - path;

    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (g_fsdev_entries[i].active && !strncmp(path, g_fsdev_entries[i].path, device_name_len)) {
            if (device) {
                *device = &g_fsdev_entries[i].fs;
            }
            if (g_fsdev_entries[i].shortcut) {
                sprintf(outpath, "/%s/%s", g_fsdev_entries[i].shortcut, colon + 1);
            } else {
                strcpy(outpath, colon + 1);
            }

            if (outpath[0] == '\0') {
                outpath[0] = '/';
                outpath[1] = '\0';
            }
            return 0;
        }
    }

    return -1;
}

int fsdev_wrapMountDevice(const char *name, const char* shortcut, FsFileSystem fs, bool own) {
    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (!g_fsdev_entries[i].active) {
            g_fsdev_entries[i].active = true;
            g_fsdev_entries[i].fs = fs;
            g_fsdev_entries[i].shortcut = shortcut;
            g_fsdev_entries[i].own = own;
            strncpy(g_fsdev_entries[i].path, name, sizeof(g_fsdev_entries[i].path));
            return 0;
        }
    }

    return -1;
}

void fsdev_wrapUnmountAll(void) {
    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (!g_fsdev_entries[i].active) {
            g_fsdev_entries[i].active = false;
            if (g_fsdev_entries[i].own) {
                fsFsClose(&g_fsdev_entries[i].fs);
            }
        }
    }
}
