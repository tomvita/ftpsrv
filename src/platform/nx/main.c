/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#include <ftpsrv.h>
#include "utils.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <switch/services/bsd.h>
#include <switch.h>
#include <minIni.h>

#define TEXT_NORMAL "\033[0m"
#define TEXT_RED "\033[0;31m"
#define TEXT_GREEN "\033[0;32m"
#define TEXT_YELLOW "\033[0;33m"
#define TEXT_BLUE "\033[0;34m"

struct CallbackData {
    enum FTP_API_LOG_TYPE type;
    char msg[1024];
};

static const char INI_PATH[FS_MAX_PATH] = {"/config/ftpsrv/config.ini"};
static struct FtpSrvConfig g_ftpsrv_config = {0};
static struct FtpSrvDevice g_devices[32] = {0};
static int g_devices_count;

static PadState g_pad;
static Mutex g_mutex;
static struct CallbackData* g_callback_data = NULL;
static u32 g_num_events = 0;
static volatile bool g_should_exit = false;

static bool IsApplication(void) {
    const AppletType type = appletGetAppletType();
    return type == AppletType_Application || type == AppletType_SystemApplication;
}

static void add_device(const char* path) {
    if (g_devices_count < 32) {
        sprintf(g_devices[g_devices_count++].mount, "%s:", path);
    }
}

static void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    mutexLock(&g_mutex);
        g_num_events++;
        g_callback_data = realloc(g_callback_data, g_num_events * sizeof(*g_callback_data));
        g_callback_data[g_num_events-1].type = type;
        strcpy(g_callback_data[g_num_events-1].msg, msg);
    mutexUnlock(&g_mutex);
}

static void processEvents(void) {
    mutexLock(&g_mutex);
    if (g_num_events) {
        for (int i = 0; i < g_num_events; i++) {
            switch (g_callback_data[i].type) {
                case FTP_API_LOG_TYPE_COMMAND:
                    printf(TEXT_BLUE "Command:  %s" TEXT_NORMAL "\n", g_callback_data[i].msg);
                    break;
                case FTP_API_LOG_TYPE_RESPONSE:
                    printf(TEXT_GREEN "Response: %s" TEXT_NORMAL "\n", g_callback_data[i].msg);
                    break;
                case FTP_API_LOG_TYPE_ERROR:
                    printf(TEXT_RED "Error:    %s" TEXT_NORMAL "\n", g_callback_data[i].msg);
                    break;
            }
        }

        g_num_events = 0;
        free(g_callback_data);
        g_callback_data = NULL;
        consoleUpdate(NULL);
    }
    mutexUnlock(&g_mutex);
}

static void ftp_thread(void* arg) {
    while (!g_should_exit) {
        ftpsrv_init(&g_ftpsrv_config);
        while (!g_should_exit) {
            if (ftpsrv_loop(500) != FTP_API_LOOP_ERROR_OK) {
                svcSleepThread(1000000000);
                break;
            }
        }
        ftpsrv_exit();
    }
}

static void consolePrint(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    consoleUpdate(NULL);
}

static int error_loop(const char* msg) {
    printf("Error: %s\n\n", msg);
    printf("Modify the config at: %s\n\n", INI_PATH);
    printf("\tPress (+) to exit...\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        padUpdate(&g_pad);
        const u64 kDown = padGetButtonsDown(&g_pad);
        if (kDown & HidNpadButton_Plus) {
            break;
        }
        svcSleepThread(1e+9 / 60);
    }
    return EXIT_FAILURE;
}

int main(int argc, char** argv) {
    consolePrint("\n[ftpsrv 0.1.0 By TotalJustice]\n\n");

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    g_ftpsrv_config.log_callback = ftp_log_callback;
    g_ftpsrv_config.anon = ini_getl("Login", "anon", 0, INI_PATH);
    const int user_len = ini_gets("Login", "user", "", g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    const int pass_len = ini_gets("Login", "pass", "", g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Network", "port", 21, INI_PATH);
    const bool mount_devices = ini_getl("Nx", "mount_devices", 1, INI_PATH);

    if (mount_devices) {
        char save_id_str[21] = {0};
        ini_gets("Nx", "save_application_id", "", save_id_str, sizeof(save_id_str), INI_PATH);

        char bcat_id_str[21] = {0};
        ini_gets("Nx", "bcat_application_id", "", bcat_id_str, sizeof(bcat_id_str), INI_PATH);

        const u64 save_id = strtoull(save_id_str, NULL, 16);
        const u64 bcat_id = strtoull(bcat_id_str, NULL, 16);

        if (R_SUCCEEDED(fsdev_wrapMountImage("image_nand", FsImageDirectoryId_Nand))) {
            add_device("image_nand");
        }
        if (R_SUCCEEDED(fsdev_wrapMountImage("image_sd", FsImageDirectoryId_Sd))) {
            add_device("image_sd");
        }

        AccountUid uid;
        if (R_SUCCEEDED(accountTrySelectUserWithoutInteraction(&uid, false))) {
            if (R_SUCCEEDED(fsdev_wrapMountSave("save", save_id, uid))) {
                add_device("save");
            }
        }

        if (R_SUCCEEDED(fsdev_wrapMountSaveBcat("bcat", bcat_id))) {
            add_device("bcat");
        }

        // add some shortcuts.
        FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
        if (sdmc) {
            if (!fsdev_wrapMountDevice("switch", "/switch", *sdmc, false)) {
                add_device("switch");
            }

            if (!fsdev_wrapMountDevice("contents", "/atmosphere/contents", *sdmc, false)) {
                add_device("contents");
            }
        }

        g_ftpsrv_config.devices = g_devices;
        g_ftpsrv_config.devices_count = g_devices_count;
    }

    if (!user_len && !pass_len && !g_ftpsrv_config.anon) {
        return error_loop("User / Pass / Anon not set in config!");
    }

    if (!g_ftpsrv_config.port) {
        return error_loop("Network: Invalid port");
    }

    u32 ip;
    if (R_FAILED(nifmGetCurrentIpAddress(&ip))) {
        return error_loop("failed to get current ip address");
    }

    const struct in_addr addr = {ip};
    printf(TEXT_YELLOW "ip: %s\n", inet_ntoa(addr));
    printf(TEXT_YELLOW "port: %d" TEXT_NORMAL "\n", g_ftpsrv_config.port);
    if (g_ftpsrv_config.anon) {
        printf(TEXT_YELLOW "anon: %d" TEXT_NORMAL "\n", 1);
    } else {
        printf(TEXT_YELLOW "user: %s" TEXT_NORMAL "\n", g_ftpsrv_config.user);
        printf(TEXT_YELLOW "pass: %s" TEXT_NORMAL "\n", g_ftpsrv_config.pass);
    }
    printf(TEXT_YELLOW "\nconfig: %s" TEXT_NORMAL "\n", INI_PATH);
    printf("\n");
    consoleUpdate(NULL);

    mutexInit(&g_mutex);
    Thread thread;
    if (R_FAILED(threadCreate(&thread, ftp_thread, NULL, NULL, 1024*16, 0x31, 1))) {
        error_loop("threadCreate() failed");
    } else {
        if (R_FAILED(threadStart(&thread))) {
            error_loop("threadStart() failed");
        } else {
            while (appletMainLoop()) {
                padUpdate(&g_pad);
                const u64 kDown = padGetButtonsDown(&g_pad);
                if (kDown & HidNpadButton_Plus) {
                    break;
                }

                processEvents();
                svcSleepThread(1e+9 / 60);
            }
            g_should_exit = 1;
            threadWaitForExit(&thread);
        }
        threadClose(&thread);
    }

    if (g_callback_data) {
        free(g_callback_data);
    }
}

u32 __nx_fs_num_sessions = 2;

#define TCP_TX_BUF_SIZE (1024 * 1024 * 1)
#define TCP_RX_BUF_SIZE (1024 * 1024 * 1)
#define TCP_TX_BUF_SIZE_MAX (1024 * 1024 * 4)
#define TCP_RX_BUF_SIZE_MAX (1024 * 1024 * 4)
#define UDP_TX_BUF_SIZE (0)
#define UDP_RX_BUF_SIZE (0)
#define SB_EFFICIENCY (12)

#define SOCKET_TMEM_SIZE \
    ((((TCP_TX_BUF_SIZE_MAX ? TCP_TX_BUF_SIZE_MAX : TCP_TX_BUF_SIZE) \
    + (TCP_RX_BUF_SIZE_MAX ? TCP_RX_BUF_SIZE_MAX : TCP_RX_BUF_SIZE)) \
    + UDP_TX_BUF_SIZE + UDP_RX_BUF_SIZE + 0xFFF) &~ 0xFFF) * SB_EFFICIENCY

alignas(0x1000) static u8 SOCKET_TRANSFER_MEM[SOCKET_TMEM_SIZE];

static u32 socketSelectVersion(void) {
    if (hosversionBefore(3,0,0)) {
        return 1;
    } else if (hosversionBefore(4,0,0)) {
        return 2;
    } else if (hosversionBefore(5,0,0)) {
        return 3;
    } else if (hosversionBefore(6,0,0)) {
        return 4;
    } else if (hosversionBefore(8,0,0)) {
        return 5;
    } else if (hosversionBefore(9,0,0)) {
        return 6;
    } else if (hosversionBefore(13,0,0)) {
        return 7;
    } else if (hosversionBefore(16,0,0)) {
        return 8;
    } else /* latest known version */ {
        return 9;
    }
}

void userAppInit(void) {
    Result rc;

    // https://github.com/mtheall/ftpd/blob/e27898f0c3101522311f330e82a324861e0e3f7e/source/switch/init.c#L31
    // this allocates 96MiB of memory (4Mib * 2 * 12)
    const SocketInitConfig socket_config = {
        .tcp_tx_buf_size     = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size     = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,
        .udp_tx_buf_size     = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size     = UDP_RX_BUF_SIZE,
        .sb_efficiency       = SB_EFFICIENCY,
        .num_bsd_sessions    = 2,
        .bsd_service_type    = BsdServiceType_Auto,
    };

    const BsdInitConfig bsd_config = {
        .version             = socketSelectVersion(),

        .tmem_buffer         = SOCKET_TRANSFER_MEM,
        .tmem_buffer_size    = sizeof(SOCKET_TRANSFER_MEM),

        .tcp_tx_buf_size     = socket_config.tcp_tx_buf_size,
        .tcp_rx_buf_size     = socket_config.tcp_rx_buf_size,
        .tcp_tx_buf_max_size = socket_config.tcp_tx_buf_max_size,
        .tcp_rx_buf_max_size = socket_config.tcp_rx_buf_max_size,

        .udp_tx_buf_size     = socket_config.udp_tx_buf_size,
        .udp_rx_buf_size     = socket_config.udp_rx_buf_size,

        .sb_efficiency       = socket_config.sb_efficiency,
    };

    if (R_FAILED(rc = appletLockExit()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = bsdInitialize(&bsd_config, socket_config.num_bsd_sessions, socket_config.bsd_service_type)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = socketInitialize(&socket_config)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nifmInitialize(NifmServiceType_User)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = accountInitialize(IsApplication() ? AccountServiceType_Application : AccountServiceType_System)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = fsdev_wrapMountSdmc()))
        diagAbortWithResult(rc);

    add_device("sdmc");
    consoleInit(NULL);
}

void userAppExit(void) {
    accountExit();
    socketExit();
    bsdExit();
    nifmExit();
    consoleExit(NULL);
    fsdev_wrapUnmountAll();
    appletUnlockExit();
}