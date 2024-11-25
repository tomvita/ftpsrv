// Copyright 2024 TotalJustice.
// SPDX-License-Identifier: MIT
#ifndef FTP_SRV_SOCKET_H
#define FTP_SRV_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#if 0
int socket_open(int domain, int type, int protocol);
int socket_recv(int fd, void* mem, int size, int flags);
int socket_send(int fd, const void* data, int size, int flags);
int socket_close(int fd);
int socket_shutdown(int fd, int how);
int socket_accept(int fd, struct sockaddr* addr, socklen_t* addrlen);
int socket_bind(int fd, struct sockaddr* addr, socklen_t addrlen);
int socket_connect(int fd,struct sockaddr* addr, socklen_t addrlen);
int socket_listen(int fd, int backlog);
int socket_getsockname(int fd, struct sockaddr* addr, socklen_t* addrlen);
int socket_setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen);
int socket_fcntl(int fd, int cmd, int flags);
int socket_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);
int socket_poll(struct pollfd* fds, int nsds, int timeout);
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
