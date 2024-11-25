#include "utils.h"
#include <string.h>

#define FsDevWrap_DEVICES_MAX 32

#define FsDevWrap_Module 0x505

enum FsDevWrap_Error {
    FsDevWrap_Error_FailedToMount = MAKERESULT(FsDevWrap_Module, 0x1),
};

struct FsDevWrapEntry {
    FsFileSystem fs;
    char path[32];
    bool active;
};

static struct FsDevWrapEntry g_fsdev_entries[FsDevWrap_DEVICES_MAX] = {0};

static Result mount_helper(const char* path, FsFileSystem fs) {
    if (fsdev_wrapMountDevice(path, fs)) {
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
            *device = &g_fsdev_entries[i].fs;
            strcpy(outpath, colon + 1);
            if (outpath[0] == '\0') {
                outpath[0] = '/';
                outpath[1] = '\0';
            }
            return 0;
        }
    }

    return -1;
}

int fsdev_wrapMountDevice(const char *name, FsFileSystem fs) {
    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (!g_fsdev_entries[i].active) {
            g_fsdev_entries[i].active = 1;
            g_fsdev_entries[i].fs = fs;
            strncpy(g_fsdev_entries[i].path, name, sizeof(g_fsdev_entries[i].path));
            return 0;
        }
    }

    return -1;
}

void fsdev_wrapUnmountAll(void) {
    for (int i = 0; i < FsDevWrap_DEVICES_MAX; i++) {
        if (!g_fsdev_entries[i].active) {
            g_fsdev_entries[i].active = 0;
            fsFsClose(&g_fsdev_entries[i].fs);
        }
    }
}
