/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#include "ftpsrv.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define TEXT_NORMAL "\033[0m"
#define TEXT_RED "\033[0;31m"
#define TEXT_GREEN "\033[0;32m"
#define TEXT_BLUE "\033[0;34m"

static void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    switch (type) {
        case FTP_API_LOG_TYPE_COMMAND:
            printf(TEXT_BLUE "Command:  %s" TEXT_NORMAL "\n", msg);
            break;
        case FTP_API_LOG_TYPE_RESPONSE:
            printf(TEXT_GREEN "Response: %s" TEXT_NORMAL "\n", msg);
            break;
        case FTP_API_LOG_TYPE_ERROR:
            printf(TEXT_RED "Error:    %s" TEXT_NORMAL "\n", msg);
            break;
    }
}

int main(int argc, char** argv) {
    const struct FtpSrvConfig ftpsrv_config = {
        .log_callback = ftp_log_callback,
        .port = 5002,
        .anon = 0,
        .user = "total",
        .pass = "total",
    };

    while (1) {
        ftpsrv_init(&ftpsrv_config);
        while (1) {
            if (ftpsrv_loop(-1) != FTP_API_LOOP_ERROR_OK) {
                sleep(1);
                break;
            }
        }
        ftpsrv_exit();
    }
}
