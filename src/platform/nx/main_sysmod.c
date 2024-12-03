#include "ftpsrv.h"
#include "utils.h"
#include "log/log.h"

#include <string.h>
#include <switch.h>
#include <switch/services/bsd.h>
#include <minIni.h>

static const char* INI_PATH = "/config/ftpsrv/config.ini";
static const char* LOG_PATH = "/config/ftpsrv/log.txt";
static struct FtpSrvConfig g_ftpsrv_config = {0};
static struct FtpSrvDevice g_devices[FsDevWrap_DEVICES_MAX] = {0};
static int g_devices_count = 0;
static bool g_led_enabled = false;

static void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    log_file_write(msg);
    if (g_led_enabled) {
        led_flash();
    }
}

static void add_device(const char* path) {
    if (g_devices_count < FsDevWrap_DEVICES_MAX) {
        sprintf(g_devices[g_devices_count++].mount, "%s:", path);
    }
}

int main(void) {
    memset(&g_ftpsrv_config, 0, sizeof(g_ftpsrv_config));

    g_ftpsrv_config.log_callback = ftp_log_callback;
    g_ftpsrv_config.anon = ini_getbool("Login", "anon", 0, INI_PATH);
    const int user_len = ini_gets("Login", "user", "", g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    const int pass_len = ini_gets("Login", "pass", "", g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Network", "sys-port", 5001, INI_PATH);
    const bool log_enabled = ini_getbool("Log", "log", 0, INI_PATH);
    const bool mount_devices = ini_getbool("Nx", "mount_devices", 1, INI_PATH);
    g_led_enabled = ini_getbool("Nx", "led", 1, INI_PATH);

    if (log_enabled) {
        log_file_init(LOG_PATH, "ftpsrv - 0.2.0 - NX-sys");
    }

    if (mount_devices) {
        if (R_SUCCEEDED(fsdev_wrapMountImage("image_nand", FsImageDirectoryId_Nand))) {
            add_device("image_nand");
        }
        if (R_SUCCEEDED(fsdev_wrapMountImage("image_sd", FsImageDirectoryId_Sd))) {
            add_device("image_sd");
        }

        // add some shortcuts.
        FsFileSystem* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
        if (sdmc) {
            if (!fsdev_wrapMountDevice("switch", "/switch", *sdmc, false)) {
                add_device("switch");
            }
            if (!fsdev_wrapMountDevice("breeze", "/switch/breeze", *sdmc, false)) {
                add_device("breeze");
            }
            if (!fsdev_wrapMountDevice("cheats", "/switch/breeze/cheats", *sdmc, false)) {
                add_device("cheats");
            }
            if (!fsdev_wrapMountDevice("contents", "/atmosphere/contents", *sdmc, false)) {
                add_device("contents");
            }
            char game_cheat_dir_str[128] = {0};
            static char game_cheat_dir_path[160] = "/switch/breeze/cheats/";
            ini_gets("Nx", "game_cheat_dir", "", game_cheat_dir_str, sizeof(game_cheat_dir_str), INI_PATH);
            strcat(game_cheat_dir_path,game_cheat_dir_str);
            if (!fsdev_wrapMountDevice(game_cheat_dir_str, game_cheat_dir_path, *sdmc, false)) {
                add_device(game_cheat_dir_str);
            };
        }

        g_ftpsrv_config.devices = g_devices;
        g_ftpsrv_config.devices_count = g_devices_count;
    }

    // exit early as this is a security risk due to ldn-mitm.
    if (!user_len && !pass_len && !g_ftpsrv_config.anon) {
        log_file_write("User / Pass / Anon not set in config!");
        return EXIT_FAILURE;
    }

    while (1) {
        ftpsrv_init(&g_ftpsrv_config);
        while (1) {
            if (ftpsrv_loop(-1) != FTP_API_LOOP_ERROR_OK) {
                svcSleepThread(1000000000);
                break;
            }
        }
        ftpsrv_exit();
    }
}

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

#define TCP_TX_BUF_SIZE (0x1000)
#define TCP_RX_BUF_SIZE (0x1000)
#define TCP_TX_BUF_SIZE_MAX (0x4000)
#define TCP_RX_BUF_SIZE_MAX (0x4000)
#define UDP_TX_BUF_SIZE (0)
#define UDP_RX_BUF_SIZE (0)
#define SB_EFFICIENCY (4)

#define SOCKET_TMEM_SIZE \
    ((((TCP_TX_BUF_SIZE_MAX ? TCP_TX_BUF_SIZE_MAX : TCP_TX_BUF_SIZE) \
    + (TCP_RX_BUF_SIZE_MAX ? TCP_RX_BUF_SIZE_MAX : TCP_RX_BUF_SIZE)) \
    + UDP_TX_BUF_SIZE + UDP_RX_BUF_SIZE + 0xFFF) &~ 0xFFF) * SB_EFFICIENCY

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

void __libnx_init_time(void);

// Newlib heap configuration function (makes malloc/free work).
void __libnx_initheap(void) {
    alignas(0x1000) static char inner_heap[0x1000];
    extern char* fake_heap_start;
    extern char* fake_heap_end;

    // Configure the newlib heap.
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc;

    if (R_FAILED(rc = smInitialize()))
        diagAbortWithResult(rc);

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    alignas(0x1000) static u8 SOCKET_TRANSFER_MEM[SOCKET_TMEM_SIZE];

    const SocketInitConfig socket_config = {
        .tcp_tx_buf_size     = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size     = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,
        .udp_tx_buf_size     = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size     = UDP_RX_BUF_SIZE,
        .sb_efficiency       = SB_EFFICIENCY,
        .num_bsd_sessions    = 1,
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

    if (R_FAILED(rc = timeInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = fsInitialize()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = fsdev_wrapMountSdmc()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = bsdInitialize(&bsd_config, socket_config.num_bsd_sessions, socket_config.bsd_service_type)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = socketInitialize(&socket_config)))
        diagAbortWithResult(rc);

    hidsysInitialize();
    __libnx_init_time();
    smExit(); // Close SM as we don't need it anymore.

    add_device("sdmc");
}

// Service deinitialization.
void __appExit(void) {
    log_file_exit();
    hidsysExit();
    socketExit();
    bsdExit();
    fsdev_wrapUnmountAll();
    fsExit();
    timeExit();
}
