#include <string.h>
#include <stdbool.h>

#include <switch.h>
#include "ams_bpc.h"
#include "reboot_to_payload.h"
#include "../utils.h"

#define IRAM_PAYLOAD_MAX_SIZE 0x24000

bool validate_payload_from_file(FsFile* file, bool check_hekate) {
    s64 size;
    if (R_FAILED(fsFileGetSize(file, &size))) {
        return false;
    }

    if (size > IRAM_PAYLOAD_MAX_SIZE) {
        return false;
    }

    if (check_hekate) {
        u32 magic;
        u64 bytes_read;
        if (R_FAILED(fsFileRead(file, 0x118, &magic, sizeof(magic), 0, &bytes_read))) {
            return false;
        }

        if (bytes_read != 4 || magic != 0x43544349) {
            return false;
        }
    }

    return true;
}

bool validate_payload_from_path(const char* path, bool check_hekate) {
    FsFileSystem* fs;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return false;
    }

    FsFile file;
    if (R_FAILED(fsFsOpenFile(fs, nxpath, FsOpenMode_Read, &file))) {
        return false;
    }

    const bool result = validate_payload_from_file(&file, check_hekate);
    fsFileClose(&file);
    return result;
}

bool is_r2p_supported(void) {
    Result rc;
    bool can_reboot;
    if (R_FAILED(rc = setsysInitialize())) {
        can_reboot = false;
    } else {
        SetSysProductModel model;
        rc = setsysGetProductModel(&model);
        setsysExit();
        if (R_FAILED(rc) || (model != SetSysProductModel_Nx && model != SetSysProductModel_Copper)) {
            can_reboot = false;
        } else {
            can_reboot = true;
        }
    }
    return can_reboot;
}

bool reboot_to_payload(u8* iwram_buf, u32 size) {
    if (size > IRAM_PAYLOAD_MAX_SIZE) {
        return false;
    }

    if (!is_r2p_supported()) {
        return false;
    }

    Result rc;
    if (R_FAILED(rc = spsmInitialize())) {
        return false;
    }

    smExit(); //Required to connect to ams:bpc
    rc = amsBpcInitialize();
    smInitialize();
    if (R_FAILED(rc)) {
        return false;
    }

    rc = amsBpcSetRebootPayload(iwram_buf, IRAM_PAYLOAD_MAX_SIZE);
    amsBpcExit();

    if (R_SUCCEEDED(rc)) {
        rc = spsmShutdown(true);
    }

    spsmExit();
    return R_SUCCEEDED(rc);
}
