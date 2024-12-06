#include "log.h"
#include "ftpsrv_vfs.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static struct FtpVfsFile g_log_file = {0};
static int g_has_log_file = 0;

void log_file_write(const char* msg) {
    if (g_has_log_file) {
        const size_t len = strlen(msg);
        if (len) {
            ftp_vfs_write(&g_log_file, msg, len);
            if (msg[len - 1] != '\n') {
                ftp_vfs_write(&g_log_file, "\n", 1);
            }
        }
    }
}

void log_file_fwrite(const char* fmt, ...) {
    if (g_has_log_file) {
        char buf[256];
        va_list va;
        va_start(va, fmt);
        vsnprintf(buf, sizeof(buf), fmt, va);
        va_end(va);
        log_file_write(buf);
    }
}

void log_file_init(const char* path, const char* init_msg) {
    if (!g_has_log_file) {
        ftp_vfs_unlink(path);
        g_has_log_file = !ftp_vfs_open(&g_log_file, path, FtpVfsOpenMode_APPEND);
        log_file_write(init_msg);
    }
}

void log_file_exit(void) {
    if (g_has_log_file) {
        log_file_write("goodbye :)");
        ftp_vfs_close(&g_log_file);
        g_has_log_file = 0;
    }
}
