#include "custom_commands.h"
#include "reboot_to_payload/reboot_to_payload.h"
#include "rtc/max77620-rtc.h"
#include "vfs_nx.h"
#include "utils.h"
#include "minIni.h"
#include "ftpsrv_vfs.h"

#include <switch.h>
#include <switch/services/bsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// large enough size as to not explode the stack.
#define HEKATE_INI_NAME_MAX 128
// max supported by rtc.
#define HEKATE_CONFIG_MAX 16
// reasonable max config name max.
#define HEKATE_SECTION_NAME_MAX 128
// default error code for custom commands.
#define FTP_DEFAULT_ERROR_CODE 500
// title id for qlaunch.
#define QLAUNCH_TID 0x0100000000001000ULL

struct HekateListIni {
    char* msg_buf;
    unsigned msg_buf_len;
    unsigned off;
    char current_section[HEKATE_SECTION_NAME_MAX];
};

struct HekateConfigIni {
    const char* wanted;
    char current_section[HEKATE_SECTION_NAME_MAX];
    int count;
    bool found;
};

// common paths for hekate, sorted in priority.
static const char* HEKATE_PATHS[] = {
    "/bootloader/update.bin",
    "/atmosphere/reboot_payload.bin",
    "/payload.bin",
};

static int ini_browse_callback(const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) {
    struct HekateConfigIni* h = UserData;

    // may be NULL
    if (!Section || !strcmp(Section, "config")) {
        return 1;
    }

    // check if its the same as before.
    if (!strcmp(Section, h->current_section)) {
        return 1;
    }

    // check if we found the config.
    if (!strcmp(Section, h->wanted)) {
        h->found = true;
        return 0;
    }

    // add new entry.
    h->count++;
    snprintf(h->current_section, sizeof(h->current_section), "%s", Section);
    return 1;

}

static int ini_list_callback(const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) {
    struct HekateListIni* h = UserData;

    // may be NULL
    if (!Section || !strcmp(Section, "config")) {
        return 1;
    }

    // check if its the same as before.
    if (!strcmp(Section, h->current_section)) {
        return 1;
    }

    // add new entry.
    snprintf(h->current_section, sizeof(h->current_section), "%s", Section);
    h->off += snprintf(h->msg_buf + h->off, h->msg_buf_len - h->off, " %s\r\n", Section);
    if (h->off >= h->msg_buf_len) {
        return 0;
    }

    return 1;

}

static void hekate_list_config(char* msg_buf, unsigned msg_buf_len, unsigned off) {
    struct HekateListIni h = {0};
    h.msg_buf = msg_buf;
    h.msg_buf_len = msg_buf_len;
    h.off = off;

    // list configs found in ipl.ini
    ini_browse(ini_list_callback, &h, "/bootloader/hekate_ipl.ini");

    // list configs found in the ini folder
    FsFileSystem* fs;
    char nxpath[FS_MAX_PATH];
    if (!fsdev_wrapTranslatePath("/bootloader/ini", &fs, nxpath)) {
        FsDir dir;
        if (R_SUCCEEDED(fsFsOpenDirectory(fs, nxpath, FsDirOpenMode_ReadFiles|FsDirOpenMode_NoFileSize, &dir))) {
            s64 total;
            FsDirectoryEntry buf;
            char fullpath[FS_MAX_PATH];
            while (R_SUCCEEDED(fsDirRead(&dir, &total, 1, &buf)) && total == 1) {
                const int len = strlen(buf.name) - 4;
                if (len > 0 && len < HEKATE_INI_NAME_MAX && !strcasecmp(".ini", buf.name + len)) {
                    h.current_section[0] = '\0';
                    snprintf(fullpath, sizeof(fullpath), "%s/%s", nxpath, buf.name);
                    ini_browse(ini_list_callback, &h, fullpath);
                    if (h.off >= h.msg_buf_len) {
                        break;
                    }
                }
            }
            fsDirClose(&dir);
        }
    }
}

static bool hekate_get_config_idx(rtc_reboot_reason_t* rr, const char* config) {
    struct HekateConfigIni h = {0};
    h.wanted = config;

    if (!ini_browse(ini_browse_callback, &h, "/bootloader/hekate_ipl.ini")) {
        return false;
    }

    if (h.found) {
        rr->dec.autoboot_idx = h.count + 1;
        rr->dec.autoboot_list = 0;
        return true;
    }

    FsFileSystem* fs;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath("/bootloader/ini", &fs, nxpath)) {
        return false;
    }

    FsDir dir;
    if (R_FAILED(fsFsOpenDirectory(fs, nxpath, FsDirOpenMode_ReadFiles|FsDirOpenMode_NoFileSize, &dir))) {
        return false;
    }

    s32 dir_count = 0;
    char names[HEKATE_CONFIG_MAX][HEKATE_INI_NAME_MAX] = {0};

    s64 total;
    FsDirectoryEntry buf;
    while (R_SUCCEEDED(fsDirRead(&dir, &total, 1, &buf)) && total == 1) {
        const int len = strlen(buf.name) - 4;
        if (len > 0 && len < HEKATE_INI_NAME_MAX && !strcasecmp(".ini", buf.name + len)) {
            snprintf(names[dir_count], len + 1, "%s", buf.name);
            dir_count++;
            if (dir_count == HEKATE_CONFIG_MAX) {
                break;
            }
        }
    }
    fsDirClose(&dir);

    // sort entries, as hekate does.
    bool did_sort = true;
    while (did_sort) {
        did_sort = false;
        for (int i = 0; i < dir_count - 1; i++) {
            if (strcmp(names[i], names[i + 1]) > 0) {
                char temp[HEKATE_INI_NAME_MAX];
                strcpy(temp, names[i]);
                strcpy(names[i], names[i + 1]);
                strcpy(names[i + 1], temp);
                did_sort = true;
            }
        }
    }

    // find entry
    h.count = 0;
    for (int i = 0; i < dir_count; i++) {
        h.current_section[0] = '\0';
        snprintf(nxpath, sizeof(nxpath), "/bootloader/ini/%s.ini", names[i]);
        if (ini_browse(ini_browse_callback, &h, nxpath)) {
            if (h.found) {
                rr->dec.autoboot_idx = h.count + 1;
                rr->dec.autoboot_list = 1;
                return true;
            }
        }
    }

    return false;
}

static int ftp_custom_cmd_REBT(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;

    if (R_FAILED(rc = spsmInitialize())) {
        snprintf(msg_buf, msg_buf_len, "Failed: spsmInitialize() 0x%08X", rc);
    } else {
        if (R_FAILED(rc = spsmShutdown(true))) {
            snprintf(msg_buf, msg_buf_len, "Failed: spsmShutdown(true) 0x%08X", rc);
        } else {
            snprintf(msg_buf, msg_buf_len, "success");
            code = 200;
        }
        spsmExit();
    }
    return code;
}

static int ftp_custom_cmd_RTOP(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    char pathname[FS_MAX_PATH] = {0};
    const int r = snprintf(pathname, sizeof(pathname), "%s", data);

    if (r <= 0 || r >= sizeof(pathname)) {
        snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments.");
    } else {
        FsFileSystem* fs;
        char nxpath[FS_MAX_PATH] = {0};
        if (fsdev_wrapTranslatePath(pathname, &fs, nxpath)) {
            snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, invalid path: %s", pathname);
        } else {
            FsFile file;
            if (R_FAILED(rc = fsFsOpenFile(fs, nxpath, FsOpenMode_Read, &file))) {
                snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, Failed to open file: %s", nxpath);
            } else {
                s64 size;
                if (R_FAILED(rc = fsFileGetSize(&file, &size))) {
                    snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, Failed to open file: %s", nxpath);
                } else {
                    if (!validate_payload_from_file(&file, false)) {
                        snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, Not a valid payload: %s", nxpath);
                    } else {
                        for (s64 off = 0; off < size; off += 0x1000) {
                            alignas(0x1000) u8 page_buf[0x1000];

                            u64 bytes_read;
                            if (R_FAILED(rc = fsFileRead(&file, off, page_buf, sizeof(page_buf), 0, &bytes_read))) {
                                snprintf(msg_buf, msg_buf_len, "Failed to read file: %s, please restart ftpsrv", nxpath);
                                break;
                            }

                            smcCopyToIram(AMS_IWRAM_OFFSET + off, page_buf, bytes_read);
                        }

                        if (R_SUCCEEDED(rc)) {
                            code = 200;
                            snprintf(msg_buf, msg_buf_len, "Bye!");
                            smcRebootToIramPayload();
                        }
                    }
                }
                fsFileClose(&file);
            }
        }
    }
    return code; /* this code / msg is never received, obviously. */
}

static int ftp_custom_cmd_RTOH(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    rtc_reboot_reason_t rr = {0};
    char* end;
    const u64 opt = strtoull(data, &end, 10);

    if (opt > 3 || data == end) {
        snprintf(msg_buf, msg_buf_len, "Failed: bad args");
    } else if (opt == 1 && (end[0] == '\0' || !hekate_get_config_idx(&rr, end + 1))) {
        code = 200;
        int rc = snprintf(msg_buf, msg_buf_len, "-Listing configs:\r\n");
        hekate_list_config(msg_buf, msg_buf_len, rc);
    } else {
        rr.dec.reason = opt;

        if (is_r2p_supported()) {
            FsFile file;
            int found_hekate = -1;
            for (int i = 0; i < sizeof(HEKATE_PATHS)/sizeof(HEKATE_PATHS[0]); i++) {
                FsFileSystem* fs;
                char nxpath[FS_MAX_PATH] = {0};
                if (!fsdev_wrapTranslatePath(HEKATE_PATHS[i], &fs, nxpath)) {
                    if (R_SUCCEEDED(rc = fsFsOpenFile(fs, nxpath, FsOpenMode_Read, &file))) {
                        if (validate_payload_from_file(&file, true)) {
                            found_hekate = i;
                            break;
                        }
                        fsFileClose(&file);
                    }
                }
            }

            if (found_hekate < 0) {
                snprintf(msg_buf, msg_buf_len, "Failed: unable to find hekate!");
            } else {
                s64 size;
                if (R_FAILED(rc = fsFileGetSize(&file, &size))) {
                    snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, Failed to open file: %s", HEKATE_PATHS[found_hekate]);
                } else {
                    if (!validate_payload_from_file(&file, false)) {
                        snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments, Not a valid payload: %s", HEKATE_PATHS[found_hekate]);
                    } else {
                        for (s64 off = 0; off < size; off += 0x1000) {
                            alignas(0x1000) u8 page_buf[0x1000];

                            u64 bytes_read;
                            if (R_FAILED(rc = fsFileRead(&file, off, page_buf, sizeof(page_buf), 0, &bytes_read))) {
                                snprintf(msg_buf, msg_buf_len, "Failed to read file: %s, please restart ftpsrv", HEKATE_PATHS[found_hekate]);
                                break;
                            }

                            if (off == 0) {
                                page_buf[0x94] = 1 << 0; // [boot_cfg] Force AutoBoot
                                page_buf[0x95] = rr.dec.autoboot_idx; // [autoboot] Load config at this index
                                page_buf[0x96] = rr.dec.autoboot_list; // [autoboot_list] Load config from list.

                                if (opt == 3) {
                                    page_buf[0x97] = 1 << 5; // boot into ums
                                    page_buf[0x98] = rr.dec.ums_idx; // mount sd card
                                }
                            }

                            smcCopyToIram(AMS_IWRAM_OFFSET + off, page_buf, bytes_read);
                        }

                        if (R_SUCCEEDED(rc)) {
                            code = 200;
                            snprintf(msg_buf, msg_buf_len, "Bye!");
                            smcRebootToIramPayload();
                        }
                    }
                }
                fsFileClose(&file);
            }
        } else {
            if (!validate_payload_from_path("/bootloader/update.bin", true)) {
                snprintf(msg_buf, msg_buf_len, "Failed: hekate not found!");
            } else {
                if (R_FAILED(rc = spsmInitialize())) {
                    snprintf(msg_buf, msg_buf_len, "Failed: spsmInitialize() 0x%08X", rc);
                } else {
                    if (R_FAILED(rc = i2cInitialize())) {
                        snprintf(msg_buf, msg_buf_len, "Failed: i2cInitialize() 0x%08X", rc);
                    } else {
                        I2cSession s;
                        if (R_FAILED(rc = i2cOpenSession(&s, I2cDevice_Max77620Rtc))) {
                            snprintf(msg_buf, msg_buf_len, "Failed: i2cOpenSession(I2cDevice_Max77620Rtc) 0x%08X", rc);
                        } else {
                            rc = max77620_rtc_set_reboot_reason(&s, &rr);
                            i2csessionClose(&s);

                            if (R_FAILED(rc)) {
                                snprintf(msg_buf, msg_buf_len, "Failed: max77620_rtc_set_reboot_reason(opts) 0x%08X", rc);
                            } else if (R_FAILED(rc = spsmShutdown(true))) {
                                snprintf(msg_buf, msg_buf_len, "Failed: spsmShutdown(true) 0x%08X", rc);
                            } else {
                                code = 200;
                                snprintf(msg_buf, msg_buf_len, "Bye!");
                            }
                        }
                        i2cExit();
                    }
                    spsmExit();
                }
            }
        }
    }
    return code;
}

static int ftp_custom_cmd_SHUT(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;

    if (R_FAILED(rc = spsmInitialize())) {
        snprintf(msg_buf, msg_buf_len, "Failed: spsmInitialize() 0x%08X", rc);
    } else {
        if (R_FAILED(rc = spsmShutdown(false))) {
            snprintf(msg_buf, msg_buf_len, "Failed: spsmShutdown(false) 0x%08X", rc);
        } else {
            snprintf(msg_buf, msg_buf_len, "success");
            code = 200;
        }
        spsmExit();
    }
    return code;
}

int ftp_custom_cmd_TID(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;

    if (R_FAILED(rc = pmdmntInitialize())) {
        snprintf(msg_buf, msg_buf_len, "Failed: pmdmntInitialize() 0x%08X", rc);
    } else {
        if (R_FAILED(rc = pminfoInitialize())) {
            snprintf(msg_buf, msg_buf_len, "Failed: pminfoInitialize() 0x%08X", rc);
        } else {
            u64 pid, tid;
            if (R_SUCCEEDED(rc = pmdmntGetApplicationProcessId(&pid))) {
                if (0x20f == pminfoGetProgramId(&tid, pid)){
                    tid = QLAUNCH_TID;
                }
            } else if (rc == 0x20f) {
                tid = QLAUNCH_TID;
            } else {
                tid = 0;
            }

            if (!tid) {
                snprintf(msg_buf, msg_buf_len, "Failed: pminfoInitialize() 0x%08X", rc);
            } else {
                code = 200;
                NcmContentId id;
                struct AppName name;
                if (tid == QLAUNCH_TID) {
                    snprintf(msg_buf, msg_buf_len, "%016lX %s", tid, "Qlaunch");
                } else if (R_SUCCEEDED(get_app_name(tid, &id, &name))) {
                    snprintf(msg_buf, msg_buf_len, "%016lX %s", tid, name.str);
                } else {
                    snprintf(msg_buf, msg_buf_len, "%016lX", tid);
                }
            }
            pminfoExit();
        }
        pmdmntExit();
    }
    return code;
}

static int ftp_custom_cmd_TIDS(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    u64 pids[0x50];
    s32 process_count;

    if (R_FAILED(rc = svcGetProcessList(&process_count, pids, 0x50))) {
        snprintf(msg_buf, msg_buf_len, "Failed: svcGetProcessList() 0x%08X", rc);
    } else {
        if (R_FAILED(rc = pminfoInitialize())) {
            snprintf(msg_buf, msg_buf_len, "Failed: pminfoInitialize() 0x%08X", rc);
        } else {
            code = 200;
            int off = snprintf(msg_buf, msg_buf_len, "-Listing TIDS\r\n");
            for (int i = 0; i < (process_count - 1); i++) {
                u64 tid;
                if (R_SUCCEEDED(rc = pminfoGetProgramId(&tid, pids[i]))) {
                    off += snprintf(msg_buf + off, msg_buf_len - off, " %016lX\r\n", tid);
                    if (off >= msg_buf_len) {
                        break;
                    }
                }
            }
            pminfoExit();
        }
    }
    return code;
}

static int ftp_custom_cmd_TRMT(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    char* end;
    u64 tid = strtoull(data, &end, 0x10);

    if (!tid || data == end) {
        snprintf(msg_buf, msg_buf_len, "Failed: bad args");
    } else {
        if (R_FAILED(rc = pmshellInitialize())) {
            snprintf(msg_buf, msg_buf_len, "Failed: pmshellInitialize() 0x%08X", rc);
        } else {
            if (R_FAILED(rc = pmshellTerminateProgram(tid))) {
                snprintf(msg_buf, msg_buf_len, "Failed: pmshellTerminateProgram(0x%016lX) 0x%08X", tid, rc);
            } else {
                code = 200;
                snprintf(msg_buf, msg_buf_len, "success");
            }
            pmshellExit();
        }
    }
    return code;
}

static int ftp_custom_cmd_ACC(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    AccountUid uid;
    AccountProfile profile;
    AccountProfileBase profilebase;

    // Initialize account service
    if (R_FAILED(rc = accountInitialize(AccountServiceType_Application))) {
        snprintf(msg_buf, msg_buf_len, "Failed: accountInitialize() 0x%08X", rc);
        return code;
    }

    // Get the last opened user
    if (R_SUCCEEDED(rc = accountGetLastOpenedUser(&uid))) {
        // Get profile for the user
        if (R_SUCCEEDED(rc = accountGetProfile(&profile, uid))) {
            // Get profile base data
            if (R_SUCCEEDED(rc = accountProfileGet(&profile, NULL, &profilebase))) {
                code = 200;
                snprintf(msg_buf, msg_buf_len, "%016lX %016lX %s", uid.uid[0], uid.uid[1], profilebase.nickname);
            }
            accountProfileClose(&profile);
        }
    }

    accountExit();
    return code;
}

// HOME: presses the HOME button, so the HOME menu (or the SwitchU daemon, which
// starts Breeze on HOME) answers as if it had been pressed on a controller.
// A virtual Pro Controller is attached through hid:dbg for the press, the way
// Breeze's PC connect does it. Some games give it no player slot (a real
// controller in player 1 and a game for one player): then the press goes
// through that real controller instead, which gets its own state back after.
#define HOME_NO_SLOT 0xFFFFFFFFU
#define HOME_BUTTON BIT(18) // Hdls keeps bit 18 for HOME

static alignas(0x1000) u8 g_home_workmem[0x1000];

static u32 home_slot(HiddbgHdlsSessionId session, HiddbgHdlsHandle handle, HiddbgHdlsNpadAssignment* as) {
    if (R_FAILED(hiddbgDumpHdlsNpadAssignmentState(session, as))) {
        return HOME_NO_SLOT;
    }
    for (s32 i = 0; i < as->total_entries && i < 0x10; i++) {
        if (as->entries[i].handle.handle == handle.handle) {
            return as->entries[i].unk_x8;
        }
    }
    return HOME_NO_SLOT;
}

#define KEY_MAX_PRESSES 16

// One press per entry, in turn; an entry can hold several buttons (a chord).
struct KeyPresses {
    u64 buttons[KEY_MAX_PRESSES];
    unsigned count;
    const char* text; // what the reply calls it
};

static Result home_press(HiddbgHdlsHandle handle, const HiddbgHdlsState* base, const struct KeyPresses* keys) {
    Result rc = 0;
    for (unsigned i = 0; i < keys->count && R_SUCCEEDED(rc); i++) {
        HiddbgHdlsState st = *base;
        st.buttons |= keys->buttons[i];
        rc = hiddbgSetHdlsState(handle, &st);
        if (R_SUCCEEDED(rc)) {
            svcSleepThread(100000000ULL);
            rc = hiddbgSetHdlsState(handle, base);
            // Long enough between presses for a menu to take each one.
            svcSleepThread(i + 1 < keys->count ? 150000000ULL : 50000000ULL);
        }
    }
    return rc;
}

static int press_keys(const struct KeyPresses* keys, char* msg_buf, unsigned msg_buf_len) {
    Result rc;
    int code = FTP_DEFAULT_ERROR_CODE;
    static HiddbgHdlsNpadAssignment as;
    static HiddbgHdlsStateList states;

    if (R_FAILED(rc = hiddbgInitialize())) {
        snprintf(msg_buf, msg_buf_len, "Failed: hiddbgInitialize() 0x%08X", rc);
        return code;
    }

    HiddbgHdlsSessionId session = {0};
    if (R_FAILED(rc = hiddbgAttachHdlsWorkBuffer(&session, g_home_workmem, sizeof(g_home_workmem)))) {
        snprintf(msg_buf, msg_buf_len, "Failed: hiddbgAttachHdlsWorkBuffer() 0x%08X", rc);
        hiddbgExit();
        return code;
    }

    HiddbgHdlsDeviceInfo info = {0};
    info.deviceType = HidDeviceType_FullKey3;
    info.npadInterfaceType = HidNpadInterfaceType_Bluetooth;
    info.singleColorBody = RGBA8_MAXALPHA(255, 255, 255);
    info.singleColorButtons = RGBA8_MAXALPHA(0, 0, 0);
    info.colorLeftGrip = RGBA8_MAXALPHA(230, 255, 0);
    info.colorRightGrip = RGBA8_MAXALPHA(0, 40, 20);
    HiddbgHdlsHandle handle = {0};
    if (R_FAILED(rc = hiddbgAttachHdlsVirtualDevice(&handle, &info))) {
        snprintf(msg_buf, msg_buf_len, "Failed: hiddbgAttachHdlsVirtualDevice() 0x%08X", rc);
    } else {
        // It has no slot for a moment, and a press sent before it has one is lost.
        u32 slot = HOME_NO_SLOT;
        for (int i = 0; i < 50 && slot == HOME_NO_SLOT; i++) {
            svcSleepThread(20000000ULL);
            slot = home_slot(session, handle, &as);
        }
        svcSleepThread(50000000ULL);

        if (slot != HOME_NO_SLOT) {
            HiddbgHdlsState base = {0};
            base.battery_level = 4;
            if (R_FAILED(rc = home_press(handle, &base, keys))) {
                snprintf(msg_buf, msg_buf_len, "Failed: hiddbgSetHdlsState() 0x%08X", rc);
            } else {
                code = 200;
                snprintf(msg_buf, msg_buf_len, "%s pressed (virtual controller, player %u)", keys->text, slot + 1);
            }
        } else if (R_FAILED(rc = hiddbgDumpHdlsNpadAssignmentState(session, &as)) ||
                   R_FAILED(rc = hiddbgDumpHdlsStates(session, &states))) {
            snprintf(msg_buf, msg_buf_len, "Failed: no slot for the virtual controller, and the controller table cannot be read 0x%08X", rc);
        } else {
            // The real controller in the lowest slot.
            s32 pick = -1;
            for (s32 i = 0; i < as.total_entries && i < 0x10; i++) {
                const u32 s = as.entries[i].unk_x8;
                if (as.entries[i].handle.handle == handle.handle || s >= 8) {
                    continue;
                }
                if (pick < 0 || s < as.entries[pick].unk_x8) {
                    pick = i;
                }
            }
            HiddbgHdlsState base = {0};
            bool found = false;
            if (pick >= 0) {
                for (s32 k = 0; k < states.total_entries && k < 0x10; k++) {
                    if (states.entries[k].handle.handle == as.entries[pick].handle.handle) {
                        base = states.entries[k].state;
                        found = true;
                    }
                }
            }
            if (!found) {
                snprintf(msg_buf, msg_buf_len, "Failed: no slot for the virtual controller and no controller to press %s through", keys->text);
            } else if (R_FAILED(rc = home_press(as.entries[pick].handle, &base, keys))) {
                snprintf(msg_buf, msg_buf_len, "Failed: hiddbgSetHdlsState() through player %u 0x%08X", as.entries[pick].unk_x8 + 1, rc);
            } else {
                code = 200;
                snprintf(msg_buf, msg_buf_len, "%s pressed (through the controller in player %u: no slot for a virtual one)", keys->text, as.entries[pick].unk_x8 + 1);
            }
        }
        hiddbgDetachHdlsVirtualDevice(handle);
    }
    hiddbgReleaseHdlsWorkBuffer(session);
    hiddbgExit();
    return code;
}

static int ftp_custom_cmd_HOME(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    const struct KeyPresses keys = { { HOME_BUTTON }, 1, "HOME" };
    return press_keys(&keys, msg_buf, msg_buf_len);
}

// KEY <press> [<press>...]: presses in turn, each a button name or several
// joined by '+' (held together), e.g. "KEY A", "KEY DOWN DOWN A", "KEY ZL+ZR".
static const struct { const char* name; u64 bit; } KEY_NAMES[] = {
    { "A", HidNpadButton_A }, { "B", HidNpadButton_B }, { "X", HidNpadButton_X }, { "Y", HidNpadButton_Y },
    { "LS", HidNpadButton_StickL }, { "RS", HidNpadButton_StickR },
    { "L", HidNpadButton_L }, { "R", HidNpadButton_R }, { "ZL", HidNpadButton_ZL }, { "ZR", HidNpadButton_ZR },
    { "PLUS", HidNpadButton_Plus }, { "MINUS", HidNpadButton_Minus },
    { "LEFT", HidNpadButton_Left }, { "UP", HidNpadButton_Up }, { "RIGHT", HidNpadButton_Right }, { "DOWN", HidNpadButton_Down },
    { "HOME", HOME_BUTTON }, { "CAPTURE", BIT(19) }, // Hdls keeps bit 19 for Capture
};

static int ftp_custom_cmd_KEY(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    static char text[128];
    struct KeyPresses keys = { {0}, 0, text };
    snprintf(text, sizeof(text), "%s", data);
    for (char* p = text; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p -= 'a' - 'A';
        }
    }
    const char* p = data;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (keys.count == KEY_MAX_PRESSES) {
            snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments: at most %u presses", KEY_MAX_PRESSES);
            return FTP_DEFAULT_ERROR_CODE;
        }
        u64 buttons = 0;
        for (;;) {
            const char* end = p;
            while (*end && *end != ' ' && *end != '+') {
                end++;
            }
            u64 bit = 0;
            for (unsigned i = 0; i < sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]); i++) {
                if (strlen(KEY_NAMES[i].name) == (size_t)(end - p) && !strncasecmp(KEY_NAMES[i].name, p, end - p)) {
                    bit = KEY_NAMES[i].bit;
                }
            }
            if (!bit) {
                snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments: unknown button '%.*s' (A B X Y L R ZL ZR LS RS PLUS MINUS UP DOWN LEFT RIGHT HOME CAPTURE)", (int)(end - p), p);
                return FTP_DEFAULT_ERROR_CODE;
            }
            buttons |= bit;
            p = end;
            if (*p != '+') {
                break;
            }
            p++;
        }
        keys.buttons[keys.count++] = buttons;
    }
    if (!keys.count) {
        snprintf(msg_buf, msg_buf_len, "Syntax error in parameters or arguments: KEY <button>[+<button>] ...");
        return FTP_DEFAULT_ERROR_CODE;
    }
    return press_keys(&keys, msg_buf, msg_buf_len);
}

// MEM: how full each physical memory pool is, then the processes using the
// most memory, largest first, for as many as fit in the reply. Made for the
// case where library applets (Breeze, the SwitchU menu) fail to start out of
// memory and the pools have to be read before a reboot clears them.
#define MEM_MAX_PROCESSES 0x100

struct MemProcess {
    u64 program_id;
    u64 used;
};

// Atmosphere pm:dmnt 65000 AtmosphereGetProcessInfo, which libnx doesn't wrap.
static Result mem_process_handle(u64 pid, Handle* out, u64* program_id) {
    struct {
        u64 program_id;
        u8 storage_id;
        u8 pad[7];
        u8 override_status[0x10];
    } info = {0};
    Result rc = serviceDispatchInOut(pmdmntGetServiceSession(), 65000, pid, info,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = out,
    );
    if (R_SUCCEEDED(rc)) {
        *program_id = info.program_id;
    }
    return rc;
}

static int mem_compare(const void* a, const void* b) {
    const u64 ua = ((const struct MemProcess*)a)->used;
    const u64 ub = ((const struct MemProcess*)b)->used;
    return ua < ub ? 1 : ua > ub ? -1 : 0;
}

static int ftp_custom_cmd_MEM(void* userdata, const char* data, char* msg_buf, unsigned msg_buf_len) {
    static const char* const pools[] = { "application", "applet", "system", "unsafe" };
    static u64 pids[MEM_MAX_PROCESSES];
    static struct MemProcess procs[MEM_MAX_PROCESSES];
    // the reply goes out with its code and CRLF in a buffer this size
    const unsigned len = msg_buf_len > 16 ? msg_buf_len - 16 : msg_buf_len;
    unsigned off = 0;

    off += snprintf(msg_buf + off, len - off, "MB used/total:");
    for (u64 i = 0; i < 4 && off < len; i++) {
        u64 total = 0, used = 0;
        if (R_FAILED(svcGetSystemInfo(&total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, i)) ||
            R_FAILED(svcGetSystemInfo(&used, SystemInfoType_UsedPhysicalMemorySize, INVALID_HANDLE, i))) {
            off += snprintf(msg_buf + off, len - off, " %s ?", pools[i]);
        } else {
            off += snprintf(msg_buf + off, len - off, " %s %.1f/%.1f (%.1f free)", pools[i],
                used / 1048576.0, total / 1048576.0, (total - used) / 1048576.0);
        }
    }

    s32 count = 0;
    unsigned n = 0;
    if (off < len && R_SUCCEEDED(svcGetProcessList(&count, pids, MEM_MAX_PROCESSES)) &&
        R_SUCCEEDED(pmdmntInitialize())) {
        for (s32 i = 0; i < count; i++) {
            Handle handle = INVALID_HANDLE;
            u64 program_id = 0, used = 0;
            // pm doesn't track the initial processes (fs, loader, ...): skipped.
            if (R_FAILED(mem_process_handle(pids[i], &handle, &program_id))) {
                continue;
            }
            if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, handle, 0))) {
                procs[n].program_id = program_id;
                procs[n].used = used;
                n++;
            }
            svcCloseHandle(handle);
        }
        pmdmntExit();
    }

    if (n && off < len) {
        qsort(procs, n, sizeof(procs[0]), mem_compare);
        off += snprintf(msg_buf + off, len - off, "; processes MB:");
        for (unsigned i = 0; i < n && off < len; i++) {
            char entry[40];
            // system programs (01000000000xxxxx) by their last hex digits
            if ((procs[i].program_id >> 20) == 0x01000000000ULL) {
                snprintf(entry, sizeof(entry), " %lX:%.1f", procs[i].program_id & 0xFFFFF, procs[i].used / 1048576.0);
            } else {
                snprintf(entry, sizeof(entry), " %016lX:%.1f", procs[i].program_id, procs[i].used / 1048576.0);
            }
            // keep room for " ..." when the list doesn't fit
            if (off + strlen(entry) + 4 >= len) {
                off += snprintf(msg_buf + off, len - off, " ...");
                break;
            }
            off += snprintf(msg_buf + off, len - off, "%s", entry);
        }
    }
    return 200;
}

const struct FtpSrvCustomCommand CUSTOM_COMMANDS[] = {
    // reboot console
    { "REBT", ftp_custom_cmd_REBT, NULL, 1, 0 },
    // reboot to payload
    { "RTOP", ftp_custom_cmd_RTOP, NULL, 1, 1 },
    // reboot to hekate
    { "RTOH", ftp_custom_cmd_RTOH, NULL, 1, 1 },
    // shutdown console
    { "SHUT", ftp_custom_cmd_SHUT, NULL, 1, 0 },
    // list current application tid
    { "TID", ftp_custom_cmd_TID, NULL, 1, 0 },
    // list all tids
    { "TIDS", ftp_custom_cmd_TIDS, NULL, 1, 0 },
    // terminate tid
    { "TRMT", ftp_custom_cmd_TRMT, NULL, 1, 1 },
    // get current user account
    { "ACC", ftp_custom_cmd_ACC, NULL, 1, 0 },
    // press the HOME button
    { "HOME", ftp_custom_cmd_HOME, NULL, 1, 0 },
    // press buttons: KEY A, KEY DOWN A, KEY ZL+ZR
    { "KEY", ftp_custom_cmd_KEY, NULL, 1, 1 },
    // memory pools and the largest processes
    { "MEM", ftp_custom_cmd_MEM, NULL, 1, 0 },
};

const unsigned CUSTOM_COMMANDS_SIZE = sizeof(CUSTOM_COMMANDS) / sizeof(CUSTOM_COMMANDS[0]);
