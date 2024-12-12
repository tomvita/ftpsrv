#include <string.h>
#include <stdbool.h>

#include <switch.h>
#include "ams_bpc.h"
#include "reboot_to_payload.h"

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
    if (size < IRAM_PAYLOAD_MAX_SIZE) {
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
