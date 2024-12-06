/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#ifndef FTP_SRV_H
#define FTP_SRV_H

#ifdef __cplusplus
extern "C" {
#endif

enum FTP_API_LOG_TYPE {
    FTP_API_LOG_TYPE_COMMAND,
    FTP_API_LOG_TYPE_RESPONSE,
    FTP_API_LOG_TYPE_ERROR,
};

enum FTP_API_LOOP_ERROR {
    FTP_API_LOOP_ERROR_OK,
    FTP_API_LOOP_ERROR_INIT, // call ftpsrv_exit and ftpsrv_init again
};

typedef void (*FtpSrvLogCallback)(enum FTP_API_LOG_TYPE, const char*);
typedef void (*FtpSrvProgressCallback)(void);

struct FtpSrvConfig {
    char user[128];
    char pass[128];
    unsigned port;

    // if set, anon access is allowed.
    unsigned char anon;
    // if set, file uploads are not allowed.
    unsigned char read_only;
    // if set, an account is required for storing files.
    unsigned char write_account_required;
    // if set, sessions will be closed once this is elapsed.
    unsigned timeout;

    FtpSrvLogCallback log_callback;
    FtpSrvProgressCallback progress_callback;
};

int ftpsrv_init(const struct FtpSrvConfig* cfg);
int ftpsrv_loop(int timeout_ms);
void ftpsrv_exit(void);

#if 0
int ftpsrv_config_init(struct FtpSrvConfig* cfg);
int ftpsrv_config_set_user(struct FtpSrvConfig* cfg, const char* user);
int ftpsrv_config_set_pass(struct FtpSrvConfig* cfg, const char* pass);
int ftpsrv_config_set_port(struct FtpSrvConfig* cfg, int port);
int ftpsrv_config_set_anon(struct FtpSrvConfig* cfg, int anon);
int ftpsrv_config_set_devices(struct FtpSrvConfig* cfg, const struct FtpSrvDevice* devices, unsigned count);
int ftpsrv_config_set_log_callback(struct FtpSrvConfig* cfg, FtpSrvLogCallback log_callback);
#endif

#ifdef __cplusplus
}
#endif

#endif // FTP_SRV_H
