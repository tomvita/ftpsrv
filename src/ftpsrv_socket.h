// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#ifndef FTP_SRV_SOCKET_H
#define FTP_SRV_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FTP_SOCKET_HEADER
    #include FTP_SOCKET_HEADER
#else
    #error FTP_SOCKET_HEADER not set to the header file path!
#endif

#ifdef __cplusplus
}
#endif

#endif // FTP_SRV_SOCKET_H
