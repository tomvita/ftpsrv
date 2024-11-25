/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#include "ftpsrv.h"
#include "ftpsrv_vfs.h"
#include "ftpsrv_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#if defined(HAVE_IPTOS_THROUGHPUT) && HAVE_IPTOS_THROUGHPUT
    #include <netinet/ip.h>
#endif

#if defined(HAVE_SENDFILE) && HAVE_SENDFILE
    #include <sys/sendfile.h>
#endif

// helper which returns the size of array
#define FTP_ARR_SZ(x) (sizeof(x) / sizeof(x[0]))

// number of max concurrent sessions
#ifndef FTP_MAX_SESSIONS
    #define FTP_MAX_SESSIONS 128
#endif

// size of the buffer used for file transfers
#ifndef FTP_FILE_BUFFER_SIZE
    #define FTP_FILE_BUFFER_SIZE (1024 * 64) /* 64 KiB */
#endif

// size of the max length of pathname
#ifndef FTP_PATHNAME_SIZE
    #define FTP_PATHNAME_SIZE 4096
    #define FTP_PATHNAME_SSCANF "4095"
#elif !defined(FTP_PATHNAME_SSCANF)
    #error FTP_PATHNAME_SSCANF should be the size of (FTP_PATHNAME_SIZE-1) to prevent sscanf overflow
#endif

#define TELNET_EOL "\r\n"

enum FTP_TYPE {
    FTP_TYPE_ASCII,  // unsupported
    FTP_TYPE_EBCDIC, // unsupported
    FTP_TYPE_IMAGE,
    FTP_TYPE_LOCAL,  // unsupported
};

enum FTP_MODE {
    FTP_MODE_STREAM,
    FTP_MODE_BLOCK,      // unsupported
    FTP_MODE_COMPRESSED, // unsupported
};

enum FTP_STRUCTURE {
    FTP_STRUCTURE_FILE,
    FTP_STRUCTURE_RECORD, // unsupported
    FTP_STRUCTURE_PAGE,   // unsupported
};

enum FTP_DATA_CONNECTION {
    FTP_DATA_CONNECTION_NONE,    // default, starts in control
    FTP_DATA_CONNECTION_ACTIVE,  // enabled using PORT
    FTP_DATA_CONNECTION_PASSIVE, // enabled using PASV
};

enum FTP_TRANSFER_MODE {
    FTP_TRANSFER_MODE_NONE, // no transfer
    // FTP_TRANSFER_MODE_LIST, // transfer using LIST
    // FTP_TRANSFER_MODE_NLST, // transfer using NLST
    FTP_TRANSFER_MODE_RETR, // transfer using RETR
    FTP_TRANSFER_MODE_STOR, // transfer using STOR
};

enum FTP_AUTH_MODE {
    FTP_AUTH_MODE_NONE,      // not authenticated
    FTP_AUTH_MODE_NEED_PASS, // username ok, waiting for password
    FTP_AUTH_MODE_VALID,     // authenticated
};

enum FTP_ARGS {
    FTP_ARGS_NONE,
    FTP_ARGS_OPTIONAL,
    FTP_ARGS_REQUIRED,
};

struct Pathname {
    char s[FTP_PATHNAME_SIZE];
};

struct FtpTransfer {
    enum FTP_TRANSFER_MODE mode;

    size_t offset;
    size_t size; // only set during retr

    struct FtpVfsFile file_vfs;
    // struct FtpVfsDir dir_vfs;
    // struct FtpVfsDirEntry dir_vfs_entry;

    // char list_buf[1024];
};

struct FtpSession {
    int active; // 1 = active
    enum FTP_AUTH_MODE auth_mode;

    enum FTP_TYPE type;
    enum FTP_MODE mode;
    enum FTP_STRUCTURE structure;
    enum FTP_DATA_CONNECTION data_connection;

    struct FtpTransfer transfer;

    int control_sock; // socket for commands
    int data_sock;    // socket for data (PORT/PASV)
    int pasv_sock;    // socket for PASV listen fd

    struct sockaddr_in control_sockaddr;
    struct sockaddr_in data_sockaddr;
    struct sockaddr_in pasv_sockaddr;

    int server_marker; // file offset when using REST

    struct Pathname pwd;   // current directory
    struct Pathname tpath; // rename from buffer
};

struct FtpCommand {
    const char* name;
    void (*cmd_func)(struct FtpSession* session, const char* data);
    int auth_required;
    int args_required;
};

struct Ftp {
    int initialised;
    int server_sock;

    unsigned session_count;
    struct FtpSession sessions[FTP_MAX_SESSIONS];

    unsigned char data_buf[FTP_FILE_BUFFER_SIZE];
    struct FtpSrvConfig cfg;
};

static struct Ftp g_ftp = {0};

#if !HAVE_STRNCASECMP
static int strncasecmp(const char* a, const char* b, size_t len) {
    int rc = 0;
    for (size_t i = 0; i < len; i++) {
        if ((rc = tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]))) {
            break;
        }
    }
    return rc;
}
#endif

static void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    if (g_ftp.cfg.log_callback) {
        g_ftp.cfg.log_callback(type, msg);
    }
}

static int ftp_set_socket_reuseaddr_enable(int sock) {
#if defined(HAVE_SO_REUSEADDR) && HAVE_SO_REUSEADDR
    const int option = 1;
    return socket_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
#else
    return 0;
#endif
}

static int ftp_set_socket_nodelay_enable(int sock) {
#if defined(HAVE_TCP_NODELAY) && HAVE_TCP_NODELAY
    const int option = 1;
    return socket_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option));
#else
    return 0;
#endif
}

static int ftp_set_socket_keepalive_enable(int sock) {
#if defined(HAVE_SO_KEEPALIVE) && HAVE_SO_KEEPALIVE
    const int option = 1;
    return socket_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(option));
#else
    return 0;
#endif
}

static int ftp_set_socket_oobline_enable(int sock) {
#if defined(HAVE_SO_OOBINLINE) && HAVE_SO_OOBINLINE
    const int option = 1;
    return socket_setsockopt(sock, SOL_SOCKET, SO_OOBINLINE, &option, sizeof(option));
#else
    return 0;
#endif
}

static int ftp_set_socket_throughput_enable(int sock) {
#if defined(HAVE_IPTOS_THROUGHPUT) && HAVE_IPTOS_THROUGHPUT
    const int option = IPTOS_THROUGHPUT;
    return socket_setsockopt(sock, IPPROTO_IP, IP_TOS, &option, sizeof(option));
#else
    return 0;
#endif
}

static int ftp_set_socket_nonblocking_enable(int sock) {
    int rc = socket_fcntl(sock, F_GETFL, 0);
    if (rc >= 0) {
        rc = socket_fcntl(sock, F_SETFL, rc | O_NONBLOCK);
    }
    return rc;
}

static inline unsigned socket_bind_port(void) {
    static unsigned port = 49152;
    const unsigned ret = port;
    port = (port == 65535) ? 49152 : port + 1;
    return ret;
}

#if 0
// reads as many entries as possible until either it blocks or ends.
static void ftp_list_2(struct FtpSession* session) {
    int rc = 0;
    errno = 0;
    int has_entry = 0;

    while (rc == 0) {
        if (!has_entry) {

        }

        const char* name = ftp_vfs_dirname(&session->transfer.dir_vfs, &session->transfer.dir_vfs_entry);
        if (!name) {
            rc = -1;
            break;
        }

        if (!strcmp(".", name) || !strcmp("..", name)) {
            has_entry = 0;
            continue;
        }

        struct Pathname filepath;
        rc = snprintf(filepath.s, sizeof(filepath), "%s/%s", session->tpath.s, name);
        if (rc <= 0 || rc > sizeof(filepath)) {
            continue;
        }
    }
}
#endif

// removes dangling '/' and duplicate '/' and converts '\\' to '/'
static void remove_slashes(struct Pathname* pathname) {
    int match = 0;
    size_t offset = 0;
    size_t len = strlen(pathname->s);

    while (offset < len) {
        if (pathname->s[offset] == '\\') {
            pathname->s[offset] = '/';
        }

        if (pathname->s[offset] == '/') {
            if (match == 1) {
                memmove(pathname->s + offset, pathname->s + offset + 1, len - offset);
                len--;
            } else {
                match = 1;
                offset++;
            }
        } else {
            match = 0;
            offset++;
        }
    }

    if (len > 1 && pathname->s[len - 1] == '/') {
        pathname->s[len - 1] = '\0';
    }
}

static int build_fullpath(const struct FtpSession* session, struct Pathname* out, struct Pathname pathname) {
    int rc = 0;
    remove_slashes(&pathname);

    // check if it's the fullpath
    if (pathname.s[0] == '/') {
        strcpy(out->s, pathname.s);
    } else if (!strcmp("..", pathname.s)) {
        strcpy(out->s, session->pwd.s);

        char* last_slash = strrchr(out->s, '/');
        if (!last_slash || last_slash == out->s) {
            strcpy(out->s, "/");
        } else {
            if (strlen(last_slash) > 1) {
                last_slash[0] = '\0';
            }
        }
    } else {
        if (session->pwd.s[strlen(session->pwd.s) - 1] != '/') {
            rc = snprintf(out->s, sizeof(*out), "%s/%s", session->pwd.s, pathname.s);
        } else {
            rc = snprintf(out->s, sizeof(*out), "%s%s", session->pwd.s, pathname.s);
        }
    }

    // return an error if the output was truncated or it failed.
    if (rc < 0 || rc > sizeof(*out)) {
        rc = -1;
    } else {
        rc = 0;
    }

    return rc;
}

// converts the path to be used for fs functions, such as open and opendir
// this cannot fail, unless the path is invalid, in which case it should've
// been handled in build_path error code.
// this is a no-op if devices or devices_count is 0.
static inline struct Pathname fix_path_for_device(const struct Pathname* path) {
    struct Pathname out = *path;

    if (g_ftp.cfg.devices && g_ftp.cfg.devices_count) {
        if (out.s[0] == '/' && strchr(out.s, ':')) {
            // removes the leading slash
            memmove(out.s, out.s + 1, strlen(out.s));
        }
    }

    return out;
}

// SOURCE: https://cr.yp.to/ftp/list/binls.html
static int ftp_send_list_entry(const struct FtpSession* session, const time_t cur_time, const struct Pathname* fullpath, const char* name, const struct stat* st, int nlist) {
    int rc;
    char buf[1024 * 4] = {0};

    if (nlist) {
        rc = snprintf(buf, sizeof(buf), "%s" TELNET_EOL, name);
    } else {
        static const char months[12][4] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
        };

        char perms[11] = {0};
        switch (st->st_mode & S_IFMT) {
            case S_IFREG:   perms[0] = '-'; break;
            case S_IFDIR:   perms[0] = 'd'; break;
            case S_IFLNK:   perms[0] = 'l'; break;
            case S_IFIFO:   perms[0] = 'p'; break;
            case S_IFSOCK:  perms[0] = 's'; break;
            case S_IFCHR:   perms[0] = 'c'; break;
            case S_IFBLK:   perms[0] = 'b'; break;
            default:        perms[0] = '?'; break;
        }

        perms[1] = (st->st_mode & S_IRUSR) ? 'r' : '-';
        perms[2] = (st->st_mode & S_IWUSR) ? 'w' : '-';
        perms[3] = (st->st_mode & S_IXUSR) ? 'x' : '-';
        perms[4] = (st->st_mode & S_IRGRP) ? 'r' : '-';
        perms[5] = (st->st_mode & S_IWGRP) ? 'w' : '-';
        perms[6] = (st->st_mode & S_IXGRP) ? 'x' : '-';
        perms[7] = (st->st_mode & S_IROTH) ? 'r' : '-';
        perms[8] = (st->st_mode & S_IWOTH) ? 'w' : '-';
        perms[9] = (st->st_mode & S_IXOTH) ? 'x' : '-';

        struct Pathname symlink_path = {0};
        if (perms[0] == 'l') {
            strcpy(symlink_path.s, " -> ");
            const int len = ftp_vfs_readlink(fullpath->s, symlink_path.s + strlen(symlink_path.s), sizeof(symlink_path) - 1 - strlen(symlink_path.s));
            if (len < 0) {
                symlink_path.s[0] = '\0';
            }
        }

        struct tm tm = {0};
        const struct tm* gtm = gmtime(&st->st_mtime);
        if (gtm) {
            tm = *gtm;
        }

        // if the time is greater than 6 months, show year rather than time
        char date[6] = {0};
        const time_t six_months = 60ll * 60ll * 24ll * (365ll / 2ll);
        if (labs(cur_time - st->st_mtime) > six_months) {
            snprintf(date, sizeof(date), "%5u", tm.tm_year + 1900);
        } else {
            snprintf(date, sizeof(date), "%02u:%02u", tm.tm_hour, tm.tm_min);
        }

        const unsigned nlink = st->st_nlink;
        const size_t size = S_ISDIR(st->st_mode) ? 0 : st->st_size;

        rc = snprintf(buf, sizeof(buf), "%s %3u %s %s %13zu %s %3d %s %s%s" TELNET_EOL,
            perms,
            nlink,
            ftp_vfs_getpwuid(st), ftp_vfs_getgrgid(st),
            size,
            months[tm.tm_mon], tm.tm_mday, date,
            name, symlink_path.s);
    }

    if (rc <= 0 || rc > sizeof(buf)) {
        // don't send anything on error or truncated
        rc = -1;
    } else {
        // send everything.
        rc = socket_send(session->data_sock, buf, rc, 0);
    }

    return rc;
}

static void ftp_client_msg(const struct FtpSession* session, const char* fmt, ...) {
    char buf[1024 * 4] = {0};
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, va);
    va_end(va);

    const int code = atoi(buf);
    if (code < 400) {
        ftp_log_callback(FTP_API_LOG_TYPE_RESPONSE, buf);
    } else {
        ftp_log_callback(FTP_API_LOG_TYPE_ERROR, buf);
    }

    strcat(buf, TELNET_EOL);
    socket_send(session->control_sock, buf, strlen(buf), 0);
}

// https://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
static void ftp_close_socket(int* sock) {
    // tldr: when send() returns, this does not mean that all data
    // has been sent to the server. All this means is that the data
    // has been sent to the kernel!
    // when calling close, the kernel *may* remove that data, and
    // thus the client will *not* receive it!
    if (*sock > 0) {
        socket_shutdown(*sock, SHUT_RDWR);
        socket_close(*sock);
        *sock = -1;
    }
}

static int ftp_data_open(struct FtpSession* session) {
    int rc = 0;
    ftp_client_msg(session, "150 File status okay; about to open data connection.");

    switch (session->data_connection) {
        case FTP_DATA_CONNECTION_NONE:
            rc = -1;
            break;
        case FTP_DATA_CONNECTION_ACTIVE:
            rc = session->data_sock = socket_open(PF_INET, SOCK_STREAM, 0);
            if (rc > 0) {
                rc = socket_connect(session->data_sock, (struct sockaddr*)&session->data_sockaddr, sizeof(session->data_sockaddr));
            }
            break;
        case FTP_DATA_CONNECTION_PASSIVE: {
            socklen_t socklen = sizeof(session->pasv_sockaddr);
            rc = session->data_sock = socket_accept(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, &socklen);
        }
        break;
    }

    if (rc > 0) {
        ftp_set_socket_keepalive_enable(session->data_sock);
        ftp_set_socket_throughput_enable(session->data_sock);
    }

    return rc;
}

static void ftp_data_transfer_end(struct FtpSession* session) {
    switch (session->data_connection) {
        case FTP_DATA_CONNECTION_NONE:
            break;
        case FTP_DATA_CONNECTION_ACTIVE:
            ftp_close_socket(&session->data_sock);
            break;
        case FTP_DATA_CONNECTION_PASSIVE:
            ftp_close_socket(&session->data_sock);
            ftp_close_socket(&session->pasv_sock);
            break;
    }

    ftp_vfs_close(&session->transfer.file_vfs);

    session->transfer.offset = 0;
    session->transfer.size = 0;
    session->transfer.mode = FTP_TRANSFER_MODE_NONE;
    session->data_connection = FTP_DATA_CONNECTION_NONE;
}

static void ftp_data_transfer_progress(struct FtpSession* session) {
    int n = 0;
    errno = 0;
    struct FtpTransfer* transfer = &session->transfer;

    switch (transfer->mode) {
        case FTP_TRANSFER_MODE_NONE:
            return;

        case FTP_TRANSFER_MODE_RETR:
            #if defined(FTP_VFS_FD) && defined(HAVE_SENDFILE) && HAVE_SENDFILE
            n = sendfile(session->data_sock, transfer->file_vfs.fd, NULL, transfer->size - transfer->offset);
            if (n < 0 && (errno == EINVAL || errno == ENOSYS))
            #endif
            {
                const int read = n = ftp_vfs_read(&transfer->file_vfs, g_ftp.data_buf, sizeof(g_ftp.data_buf));
                if (n > 0) {
                    n = socket_send(session->data_sock, g_ftp.data_buf, n, 0);
                    if (n >= 0 && n != read) {
                        char buf[64];
                        sprintf(buf, "%d vs %d", n, read);
                        ftp_log_callback(FTP_API_LOG_TYPE_ERROR, "partial read");
                        ftp_log_callback(FTP_API_LOG_TYPE_ERROR, buf);
                        ftp_vfs_seek(&transfer->file_vfs, transfer->offset + (size_t)n);
                    }
                } else {
                    ftp_log_callback(FTP_API_LOG_TYPE_ERROR, "vfs read failed");
                }
            }
            break;
        case FTP_TRANSFER_MODE_STOR:
            n = socket_recv(session->data_sock, g_ftp.data_buf, sizeof(g_ftp.data_buf), 0);
            if (n > 0) {
                n = ftp_vfs_write(&transfer->file_vfs, g_ftp.data_buf, n);
            }
            break;
    }

    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            ftp_log_callback(FTP_API_LOG_TYPE_ERROR, "blocking transfer!");
            ftp_vfs_seek(&transfer->file_vfs, transfer->offset);
        } else {
            ftp_client_msg(session, "426 bad Connection closed; transfer aborted. %d %s", n, strerror(errno));
            ftp_data_transfer_end(session);
        }
    } else {
        transfer->offset += n;
        if (n == 0) {
            ftp_client_msg(session, "226 Closing data connection.");
            ftp_data_transfer_end(session);
        } else if (transfer->mode == FTP_TRANSFER_MODE_RETR && transfer->offset == transfer->size) {
            ftp_client_msg(session, "226 Closing data connection.");
            ftp_data_transfer_end(session);
        }
    }
}

// USER <SP> <username> <CRLF> | 230, 530, 500, 501, 421, 331, 332
static void ftp_cmd_USER(struct FtpSession* session, const char* data) {
    char username[128] = {0};
    int rc = sscanf(data, "%127[^"TELNET_EOL"]", username);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else if (g_ftp.cfg.anon) {
        if (strcmp(username, "anonymous")) {
            ftp_client_msg(session, "530 Not logged in.");
        } else {
            session->auth_mode = FTP_AUTH_MODE_VALID;
            ftp_client_msg(session, "230 User logged in, proceed.");
        }
    } else if (strcmp(username, g_ftp.cfg.user)) {
        ftp_client_msg(session, "530 Not logged in.");
    } else {
        session->auth_mode = FTP_AUTH_MODE_NEED_PASS;
        ftp_client_msg(session, "331 User name okay, need password.");
    }
}

// PASS <SP> <password> <CRLF> | 230, 202, 530, 500, 501, 503, 421, 332
static void ftp_cmd_PASS(struct FtpSession* session, const char* data) {
    char password[128] = {0};
    int rc = sscanf(data, "%127[^"TELNET_EOL"]", password);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else if (session->auth_mode != FTP_AUTH_MODE_NEED_PASS) {
        ftp_client_msg(session, "503 Bad sequence of commands.");
    } else if (strcmp(password, g_ftp.cfg.pass)) {
        ftp_client_msg(session, "530 Not logged in.");
    } else {
        session->auth_mode = FTP_AUTH_MODE_VALID;
        ftp_client_msg(session, "230 User logged in, proceed.");
    }
}

// ACCT <SP> <account-information> <CRLF> | 230, 202, 530, 500, 501, 503, 421
static void ftp_cmd_ACCT(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "500 Syntax error, command unrecognized.");
}

// used by CDUP and CWD
static void ftp_set_directory(struct FtpSession* session, const struct Pathname* pathname) {
    struct Pathname fullpath = {0};
    int rc = build_fullpath(session, &fullpath, *pathname);

    if (rc < 0) {

    } else {
        if (strcmp("/", fullpath.s)) {
            struct stat st = {0};
            rc = ftp_vfs_stat(fix_path_for_device(&fullpath).s, &st);
            if (rc < 0 || !S_ISDIR(st.st_mode)) {
                rc = -1;
            }
        }
    }

    if (rc < 0) {
        ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
    } else {
        session->pwd = fullpath;
        ftp_client_msg(session, "200 Command okay.");
    }
}

// CWD <SP> <pathname> <CRLF> | 250, 500, 501, 502, 421, 530, 550
static void ftp_cmd_CWD(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc < 1) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        ftp_set_directory(session, &pathname);
    }
}

// CDUP <SP> <pathname> <CRLF> | 250, 500, 501, 502, 421, 530, 550
static void ftp_cmd_CDUP(struct FtpSession* session, const char* data) {
    if (!strcmp("/", session->pwd.s)) {
        ftp_client_msg(session, "550 Requested action not taken.");
    } else {
        ftp_set_directory(session, &(const struct Pathname){".."});
    }
}

// SMNT <SP> <> <CRLF> | 202, 250, 500, 501, 502, 421, 530, 550
static void ftp_cmd_SMNT(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "500 Syntax error, command unrecognized.");
}

// REIN <CRLF> | 120, 220, 220, 421, 500, 502
static void ftp_cmd_REIN(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "500 Syntax error, command unrecognized.");
}

// QUIT <SP> <password> <CRLF> | 221, 500
static void ftp_cmd_QUIT(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "221 Service closing control connection.");
}

// PORT <SP> <host-port> <CRLF> | 200, 500, 501, 421, 530
static void ftp_cmd_PORT(struct FtpSession* session, const char* data) {
    ftp_data_transfer_end(session);
    unsigned int h[4] = {0}; // ip addr
    unsigned int p[2] = {0}; // port
    int rc = sscanf(data, "%3u,%3u,%3u,%3u,%3u,%3u", &h[0], &h[1], &h[2], &h[3], &p[0], &p[1]);

    if (rc < 6) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        // convert ip string to sockaddr_in
        char ip_buf[64] = {0};
        snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", h[0], h[1], h[2], h[3]);
        rc = inet_aton(ip_buf, &session->data_sockaddr.sin_addr);
        if (rc < 0) {
            ftp_client_msg(session, "501 Syntax error in parameters or arguments. %s", strerror(errno));
        } else {
            session->data_sockaddr.sin_family = PF_INET;
            session->data_sockaddr.sin_port = htons((p[0] << 8) + p[1]);
            session->data_connection = FTP_DATA_CONNECTION_ACTIVE;
            ftp_client_msg(session, "200 Command okay.");
        }
    }
}

// PASV <CRLF> | 227, 500, 501, 502, 421, 530
static void ftp_cmd_PASV(struct FtpSession* session, const char* data) {
    ftp_data_transfer_end(session);
    int rc = session->pasv_sock = socket_open(PF_INET, SOCK_STREAM, 0);

    if (rc < 0) {
        ftp_client_msg(session, "501 open failed Syntax error in parameters or arguments.");
    } else {
        // ftp_set_socket_nonblocking_enable(session->pasv_sock);

        // copy current over ip addr
        session->pasv_sockaddr = session->control_sockaddr;
        session->pasv_sockaddr.sin_port = htons(socket_bind_port());

        rc = socket_bind(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, sizeof(session->pasv_sockaddr));
        if (rc < 0) {
            ftp_client_msg(session, "501 bind failed Syntax error in parameters or arguments. %d", rc);
        } else {
            rc = socket_listen(session->pasv_sock, 1);
            if (rc < 0) {
                ftp_client_msg(session, "501 listen failed Syntax error in parameters or arguments. %s", strerror(errno));
            } else {
                // struct sockaddr_in base_addr;
                socklen_t base_len = sizeof(session->pasv_sockaddr);
                rc = socket_getsockname(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, &base_len);

                char ip_buf[16] = {0};
                const char* addr_s = inet_ntoa(session->control_sockaddr.sin_addr);
                for (int i = 0; addr_s[i]; i++) {
                    ip_buf[i] = addr_s[i];
                    if (ip_buf[i] == '.') {
                        ip_buf[i] = ',';
                    }
                }

                const unsigned port = ntohs(session->pasv_sockaddr.sin_port);
                session->data_connection = FTP_DATA_CONNECTION_PASSIVE;
                ftp_client_msg(session, "227 Entering Passive Mode (%s,%u,%u)", ip_buf, port >> 8, port & 0xFF);
                return;
            }
        }
        ftp_close_socket(&session->pasv_sock);
    }
}

// TYPE <SP> <type-code> <CRLF> | 200, 500, 501, 504, 421, 530
static void ftp_cmd_TYPE(struct FtpSession* session, const char* data) {
    char code;
    int rc = sscanf(data, "%c", &code);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else if (code == 'A') {
        session->type = FTP_TYPE_ASCII;
        ftp_client_msg(session, "200 Command okay.");
    } else if (code == 'I') {
        session->type = FTP_TYPE_IMAGE;
        ftp_client_msg(session, "200 Command okay.");
    } else {
        ftp_client_msg(session, "504 Command not implemented for that parameter.");
    }
}

// STRU <SP> <structure-code> <CRLF> | 200, 500, 501, 504, 421, 530
static void ftp_cmd_STRU(struct FtpSession* session, const char* data) {
    char code;
    int rc = sscanf(data, "%c", &code);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else if (code == 'F') {
        session->structure = FTP_STRUCTURE_FILE;
        ftp_client_msg(session, "200 Command okay.");
    } else {
        ftp_client_msg(session, "504 Command not implemented for that parameter.");
    }
}

// MODE <SP> <mode-code> <CRLF> | 200, 500, 501, 504, 421, 530
static void ftp_cmd_MODE(struct FtpSession* session, const char* data) {
    char code;
    int rc = sscanf(data, "%c", &code);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else if (code == 'S') {
        session->mode = FTP_MODE_STREAM;
        ftp_client_msg(session, "200 Command okay.");
    } else {
        ftp_client_msg(session, "504 Command not implemented for that parameter.");
    }
}

// RETR <SP> <pathname> <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 450, 550, 500, 501, 421, 530
static void ftp_cmd_RETR(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken.");
        } else {
            rc = ftp_vfs_open(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, FtpVfsOpenMode_READ);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
            } else {
                struct stat st = {0};
                rc = ftp_vfs_fstat(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, &st);
                if (rc < 0) {
                    ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
                } else {
                    session->transfer.offset = 0;
                    session->transfer.size = st.st_size;

                    if (session->server_marker > 0) {
                        session->transfer.offset = session->server_marker;
                        rc = ftp_vfs_seek(&session->transfer.file_vfs, session->transfer.offset);
                        session->server_marker = 0;
                    }

                    if (rc < 0) {
                        ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
                    } else {
                        rc = ftp_data_open(session);
                        if (rc < 0) {
                            ftp_client_msg(session, "425 Can't open data connection. %s", strerror(errno));
                        } else {
                            ftp_set_socket_nonblocking_enable(session->data_sock);
                            session->transfer.mode = FTP_TRANSFER_MODE_RETR;
                            return;
                        }
                    }
                }
                ftp_vfs_close(&session->transfer.file_vfs);
            }
        }
    }
}

// STOR <SP> <pathname> <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 551, 552, 532, 450, 452, 553, 500, 501, 421, 530
static void ftp_cmd_STOR(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        enum FtpVfsOpenMode flags = FtpVfsOpenMode_WRITE;
        if (session->server_marker == -1) {
            flags = FtpVfsOpenMode_APPEND;
            session->server_marker = 0;
        }

        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "551 Requested action aborted: page type unknown. %s", strerror(errno));
        } else {
            rc = ftp_vfs_open(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, flags);
            if (rc < 0) {
                ftp_client_msg(session, "551 Requested action aborted: page type unknown. %s", strerror(errno));
            } else {
                rc = ftp_data_open(session);
                if (rc < 0) {
                    ftp_client_msg(session, "425 Can't open data connection. %s", strerror(errno));
                } else {
                    ftp_set_socket_nonblocking_enable(session->data_sock);
                    session->transfer.mode = FTP_TRANSFER_MODE_STOR;
                    return;
                }
                ftp_vfs_close(&session->transfer.file_vfs);
            }
        }
    }
}

#if 0
// STOU <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 551, 552, 532, 450, 452, 553, 500, 501, 421, 530
static void ftp_cmd_STOU(struct FtpSession* session, const char* data) {
    struct Pathname unique_name = {"unique_file_XXXXXX"};
    if (!mktemp(unique_name.s)) {
        ftp_client_msg(session, "553 Requested action not taken. %s", strerror(errno));
    } else {
        // unique_name
    }
}
#endif

// APPE <SP> <pathname> <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 551, 552, 532, 450, 550, 452, 553, 500, 501, 502, 421, 530
static void ftp_cmd_APPE(struct FtpSession* session, const char* data) {
    session->server_marker = -1;
    ftp_cmd_STOR(session, data);
}

// ALLO <SP> <decimal-integer> <CRLF> | 200, 202, 500, 501, 504, 421, 530
static void ftp_cmd_ALLO(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "200 Command okay.");
}

// REST <SP> <marker> <CRLF> | 500, 501, 502, 421, 530, 350
static void ftp_cmd_REST(struct FtpSession* session, const char* data) {
    int server_marker;
    int rc = sscanf(data, "%d", &server_marker);

    if (rc <= 0 || server_marker < 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        session->server_marker = server_marker;
        ftp_client_msg(session, "350 Requested file action pending further information.");
    }
}

// RNFR <SP> <pathname> <CRLF> | 450, 550, 500, 501, 502, 421, 530, 350
static void ftp_cmd_RNFR(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        rc = build_fullpath(session, &session->tpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
        } else {
            ftp_client_msg(session, "350 Requested file action pending further information.");
        }
    }
}

// RNTO <SP> <pathname> <CRLF> | 250, 532, 553, 500, 501, 502, 503, 421, 530
static void ftp_cmd_RNTO(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        if (session->tpath.s[0] == '\0') {
            ftp_client_msg(session, "503 Bad sequence of commands.");
        } else {
            struct Pathname dst_path;
            rc = build_fullpath(session, &dst_path, pathname);
            if (rc < 0) {
                ftp_client_msg(session, "553 Requested action not taken. %s", strerror(errno));
            } else {
                rc = ftp_vfs_rename(session->tpath.s, dst_path.s);
                if (rc < 0) {
                    ftp_client_msg(session, "553 Requested action not taken. %s", strerror(errno));
                } else {
                    ftp_client_msg(session, "250 Requested file action okay, completed.");
                }
            }
        }
    }

    session->tpath.s[0] = '\0';
}

// ABOR <CRLF> | 225, 226, 500, 501, 502, 421
static void ftp_cmd_ABOR(struct FtpSession* session, const char* data) {
    if (session->data_connection == FTP_DATA_CONNECTION_NONE) {
        ftp_client_msg(session, "226 Closing data connection.");
    } else {
        if (session->transfer.mode == FTP_TRANSFER_MODE_NONE) {
            ftp_data_transfer_end(session);
            ftp_client_msg(session, "225 Data connection open; no transfer in progress.");
        } else {
            ftp_data_transfer_end(session);
            ftp_client_msg(session, "426 Connection closed; transfer aborted.");
            ftp_client_msg(session, "226 Closing data connection.");
        }
    }
}

// used by DELE and RMD
static void ftp_remove_file(struct FtpSession* session, const char* data, int (*func)(const char*)) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
        } else {
            rc = func(fix_path_for_device(&fullpath).s);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
            } else {
                ftp_client_msg(session, "250 Requested file action okay, completed.");
            }
        }
    }
}

// DELE <SP> <pathname> <CRLF> | 250, 450, 550, 500, 501, 502, 421, 530
static void ftp_cmd_DELE(struct FtpSession* session, const char* data) {
    ftp_remove_file(session, data, ftp_vfs_unlink);
}

// RMD  <SP> <pathname> <CRLF> | 250, 500, 501, 502, 421, 530, 550
static void ftp_cmd_RMD(struct FtpSession* session, const char* data) {
    ftp_remove_file(session, data, ftp_vfs_rmdir);
}

// MKD  <SP> <pathname> <CRLF> | 257, 500, 501, 502, 421, 530, 550
static void ftp_cmd_MKD(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
        } else {
            rc = ftp_vfs_mkdir(fix_path_for_device(&fullpath).s);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
            } else {
                ftp_client_msg(session, "257 \"%s\" created.", fullpath.s);
            }
        }
    }
}

// PWD  <CRLF> | 257, 500, 501, 502, 421, 550
static void ftp_cmd_PWD(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "257 \"%s\" opened.", session->pwd.s);
}

// used by LIST and NLIST
static void ftp_list_directory(struct FtpSession* session, const char* data, int nlist) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    struct Pathname fullpath = {0};
    if (rc <= 0) {
        rc = 0;
        fullpath = session->pwd;
    } else {
        rc = build_fullpath(session, &fullpath, pathname);
    }

    if (rc < 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        const time_t cur_time = time(NULL);
        // check if on root and using devices
        if (g_ftp.cfg.devices && g_ftp.cfg.devices_count && !strcmp("/", fullpath.s)) {
            rc = ftp_data_open(session);
            if (rc < 0) {
                ftp_client_msg(session, "425 Can't open data connection. %s", strerror(errno));
            } else {
                struct stat st = {0};
                st.st_nlink = 1;
                st.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;

                for (int i = 0; i < g_ftp.cfg.devices_count; i++) {
                    rc = ftp_send_list_entry(session, cur_time, NULL, g_ftp.cfg.devices[i].mount, &st, nlist);
                    if (rc < 0) {
                        break;
                    }
                }
                ftp_client_msg(session, "226 Closing data connection.");
                ftp_data_transfer_end(session);
            }
        } else {
            fullpath = fix_path_for_device(&fullpath);
            struct stat st = {0};
            rc = ftp_vfs_lstat(fullpath.s, &st);
            if (rc < 0) {
                ftp_client_msg(session, "450 Requested file action not taken. %s", strerror(errno));
            } else {
                rc = ftp_data_open(session);
                if (rc < 0) {
                    ftp_client_msg(session, "425 Can't open data connection. %s", strerror(errno));
                } else {
                    if (S_ISDIR(st.st_mode)) {
                        struct FtpVfsDir dir;
                        rc = ftp_vfs_opendir(&dir, fullpath.s);
                        if (rc < 0) {
                            ftp_client_msg(session, "450 Requested file action not taken. %s", strerror(errno));
                        } else {
                            const char* name;
                            struct FtpVfsDirEntry entry;
                            while ((name = ftp_vfs_readdir(&dir, &entry))) {
                                if (!strcmp(".", name) || !strcmp("..", name)) {
                                    continue;
                                }

                                static struct Pathname filepath;
                                rc = snprintf(filepath.s, sizeof(filepath), "%s/%s", fullpath.s, name);
                                if (rc <= 0 || rc > sizeof(filepath)) {
                                    continue;
                                }

                                rc = ftp_vfs_dirlstat(&dir, &entry, filepath.s, &st);
                                if (rc < 0) {
                                    continue;
                                }

                                #if 1
                                rc = ftp_send_list_entry(session, cur_time, &filepath, name, &st, nlist);
                                if (rc < 0) {
                                    break;
                                }
                                #endif
                            }

                            ftp_client_msg(session, "226 Closing data connection.");
                            ftp_vfs_closedir(&dir);
                        }
                    } else if (!nlist) {
                        // ftp_send_list_entry(session, cur_time, &fullpath, "", &st, nlist);
                        ftp_send_list_entry(session, cur_time, &fullpath, pathname.s, &st, nlist);
                        ftp_client_msg(session, "226 Closing data connection.");
                    } else {
                        ftp_client_msg(session, "450 Requested file action not taken.");
                    }
                }
                ftp_data_transfer_end(session);
            }
        }
    }
}

// LIST [<SP> <pathname>] <CRLF> | 125, 150, 226, 250, 425, 426, 451, 450, 500, 501, 502, 421, 530
static void ftp_cmd_LIST(struct FtpSession* session, const char* data) {
    ftp_list_directory(session, data, 0);
}

// NLST [<SP> <pathname>] <CRLF> | 125, 150, 226, 250, 425, 426, 451, 450, 500, 501, 502, 421, 530
static void ftp_cmd_NLST(struct FtpSession* session, const char* data) {
    ftp_list_directory(session, data, 1);
}

// SITE [<SP> <string>] <CRLF> | 200, 202, 500, 501, 530
static void ftp_cmd_SITE(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "500 Syntax error, command unrecognized.");
}

// SYST <CRLF> | 215, 500, 501, 502, 421
static void ftp_cmd_SYST(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "215 UNIX Type: L8");
}

// STAT [<SP> <string>] <CRLF> | 211, 212, 213, 450, 500, 501, 502, 421, 530
static void ftp_cmd_STAT(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "500 Syntax error, command unrecognized.");
}

// HELP <CRLF> | 211, 214, 500, 501, 502, 421
static void ftp_cmd_HELP(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "214 ftpsrv 0.1.0 By TotalJustice.");
}

// NOOP <CRLF> | 200, 500, 421
static void ftp_cmd_NOOP(struct FtpSession* session, const char* data) {
    ftp_client_msg(session, "200 Command okay.");
}

// FEAT <CRLF> | 211, 550
static void ftp_cmd_FEAT(struct FtpSession* session, const char* data) {
    ftp_client_msg(session,
        "211-Extensions supported:" TELNET_EOL
        " SIZE" TELNET_EOL
        // " MLST modify*;perm*;size*;type*;" TELNET_EOL
        "211 END");
}

// SIZE <SP> <pathname> <CRLF> | 213, 550
static void ftp_cmd_SIZE(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = sscanf(data, "%"FTP_PATHNAME_SSCANF"[^"TELNET_EOL"]", pathname.s);

    if (rc <= 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "501 Syntax error in parameters or arguments. %s", strerror(errno));
        } else {
            struct stat st = {0};
            rc = ftp_vfs_stat(fix_path_for_device(&fullpath).s, &st);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken. %s", strerror(errno));
            } else {
                ftp_client_msg(session, "213 %d", st.st_size);
            }
        }
    }
}

static const struct FtpCommand FTP_COMMANDS[] = {
    // ACCESS CONTROL COMMANDS: https://datatracker.ietf.org/doc/html/rfc959#section-4
    { "USER", ftp_cmd_USER, 0, FTP_ARGS_REQUIRED },
    { "PASS", ftp_cmd_PASS, 0, FTP_ARGS_REQUIRED },
    { "ACCT", ftp_cmd_ACCT, 0, FTP_ARGS_REQUIRED },
    { "CWD", ftp_cmd_CWD, 1, FTP_ARGS_REQUIRED },
    { "CDUP", ftp_cmd_CDUP, 1, FTP_ARGS_NONE },
    { "SMNT", ftp_cmd_SMNT, 1, FTP_ARGS_REQUIRED },
    { "REIN", ftp_cmd_REIN, 0, FTP_ARGS_NONE },
    { "QUIT", ftp_cmd_QUIT, 0, FTP_ARGS_NONE },

    // TRANSFER PARAMETER COMMANDS
    { "PORT", ftp_cmd_PORT, 1, FTP_ARGS_REQUIRED },
    { "PASV", ftp_cmd_PASV, 1, FTP_ARGS_NONE },
    { "TYPE", ftp_cmd_TYPE, 1, FTP_ARGS_REQUIRED },
    { "STRU", ftp_cmd_STRU, 1, FTP_ARGS_REQUIRED },
    { "MODE", ftp_cmd_MODE, 1, FTP_ARGS_REQUIRED },

    // FTP SERVICE COMMANDS
    { "RETR", ftp_cmd_RETR, 1, FTP_ARGS_REQUIRED },
    { "STOR", ftp_cmd_STOR, 1, FTP_ARGS_REQUIRED },
    // { "STOU", ftp_cmd_STOU, 1, FTP_ARGS_NONE },
    { "APPE", ftp_cmd_APPE, 1, FTP_ARGS_REQUIRED },
    { "ALLO", ftp_cmd_ALLO, 1, FTP_ARGS_REQUIRED },
    { "REST", ftp_cmd_REST, 1, FTP_ARGS_REQUIRED },
    { "RNFR", ftp_cmd_RNFR, 1, FTP_ARGS_REQUIRED },
    { "RNTO", ftp_cmd_RNTO, 1, FTP_ARGS_REQUIRED },
    { "ABOR", ftp_cmd_ABOR, 0, FTP_ARGS_NONE },
    { "DELE", ftp_cmd_DELE, 1, FTP_ARGS_REQUIRED },
    { "RMD", ftp_cmd_RMD, 1, FTP_ARGS_REQUIRED },
    { "MKD", ftp_cmd_MKD, 1, FTP_ARGS_REQUIRED },
    { "PWD", ftp_cmd_PWD, 1, FTP_ARGS_NONE },
    { "LIST", ftp_cmd_LIST, 1, FTP_ARGS_OPTIONAL },
    { "NLST", ftp_cmd_NLST, 1, FTP_ARGS_OPTIONAL },
    { "SITE", ftp_cmd_SITE, 1, FTP_ARGS_REQUIRED },
    { "SYST", ftp_cmd_SYST, 0, FTP_ARGS_NONE },
    { "STAT", ftp_cmd_STAT, 1, FTP_ARGS_OPTIONAL },
    { "HELP", ftp_cmd_HELP, 0, FTP_ARGS_OPTIONAL },
    { "NOOP", ftp_cmd_NOOP, 0, FTP_ARGS_NONE },

    // extensions
    { "FEAT", ftp_cmd_FEAT, 0, FTP_ARGS_NONE },
    { "SIZE", ftp_cmd_SIZE, 1, FTP_ARGS_REQUIRED },
};

static int ftp_session_init(struct FtpSession* session) {
    struct sockaddr_in sa;
    socklen_t addr_len = sizeof(sa);

    int control_sock = socket_accept(g_ftp.server_sock, (struct sockaddr*)&sa, &addr_len);
    if (control_sock < 0) {
        return control_sock;
    } else {
        ftp_set_socket_nodelay_enable(control_sock);
        ftp_set_socket_keepalive_enable(control_sock);
        ftp_set_socket_oobline_enable(session->data_sock);

        memset(session, 0, sizeof(*session));
        session->active = 1;
        session->control_sock = control_sock;
        session->data_connection = FTP_DATA_CONNECTION_NONE;
        session->control_sockaddr = sa;
        addr_len = sizeof(session->control_sockaddr);
        socket_getsockname(session->control_sock, (struct sockaddr*)&session->control_sockaddr, &addr_len);
        strcpy(session->pwd.s, "/");

        g_ftp.session_count++;
        // printf("opening session, count: %d\n", g_ftp.session_count);
        ftp_client_msg(session, "220 Service ready for new user.");
        return 0;
    }
}

static void ftp_session_close(struct FtpSession* session) {
    if (session->active) {
        ftp_close_socket(&session->control_sock);
        ftp_data_transfer_end(session);
        memset(session, 0, sizeof(*session));
        g_ftp.session_count--;
        // printf("closing session, count: %d\n", g_ftp.session_count);
    }
}

// line may not be null terminated!
static void ftp_session_progress_line(struct FtpSession* session, const char* line, int line_len) {
    char cmd_name[5] = {0};
    int rc = sscanf(line, "%4[^"TELNET_EOL"]", cmd_name);
    if (rc <= 0) {
        ftp_client_msg(session, "500 Syntax error, command unrecognized.");
    } else {
        ftp_log_callback(FTP_API_LOG_TYPE_COMMAND, cmd_name);

        // find command and execute
        int command_id = -1;
        for (size_t i = 0; i < FTP_ARR_SZ(FTP_COMMANDS); i++) {
            if (!strncasecmp(cmd_name, FTP_COMMANDS[i].name, strlen(FTP_COMMANDS[i].name))) {
                command_id = i;
                break;
            }
        }

        if (command_id < 0) {
            ftp_client_msg(session, "500 Syntax error, command unrecognized.");
        } else {
            const struct FtpCommand* cmd = &FTP_COMMANDS[command_id];
            if (cmd->auth_required && session->auth_mode != FTP_AUTH_MODE_VALID) {
                ftp_client_msg(session, "530 Not logged in.");
            } else {
                // const char* cmd_args = strchr(line + strlen(cmd->name), ' ');
                const char* cmd_args = memchr(line + strlen(cmd->name), ' ', line_len - strlen(cmd->name));
                if (cmd_args) {
                    if (cmd->args_required == FTP_ARGS_NONE) {
                        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
                    } else {
                        cmd->cmd_func(session, cmd_args + 1);
                    }
                } else {
                    if (cmd->args_required == FTP_ARGS_REQUIRED) {
                        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
                    }
                    else {
                        cmd->cmd_func(session, "\0");
                    }
                }
            }
        }
    }
}

static void ftp_session_poll(struct FtpSession* session) {
    memset(g_ftp.data_buf, 0, sizeof(g_ftp.data_buf));

    int rc = socket_recv(session->control_sock, g_ftp.data_buf, sizeof(g_ftp.data_buf) - 1, 0);
    if (rc < 0) {
        // printf("closing session due to recv error\n");
        ftp_session_close(session);
    } else if (rc == 0) {
        // printf("closing session due to rc error\n");
        ftp_session_close(session);
    } else {
        size_t line_offset = 0;
        while (1) {
            const char* end_line = strstr((char*)g_ftp.data_buf + line_offset, TELNET_EOL);
            const char* line = (char*)g_ftp.data_buf + line_offset;
            const int line_len = end_line + strlen(TELNET_EOL) - ((char*)g_ftp.data_buf + line_offset);
            if (!end_line) {
                break;
            }

            // printf("got recv %.*s\n", line_len - 2, line);
            ftp_session_progress_line(session, line, line_len);
            line_offset += line_len;
        }
    }
}

int ftpsrv_init(const struct FtpSrvConfig* cfg) {
    int rc;

    if (g_ftp.initialised || !cfg) {
        rc = -1;
    } else {
        memset(&g_ftp, 0, sizeof(g_ftp));
        memcpy(&g_ftp.cfg, cfg, sizeof(*cfg));
        g_ftp.initialised = 1;

        rc = g_ftp.server_sock = socket_open(PF_INET, SOCK_STREAM, 0);
        if (rc < 0) {
        } else {
            ftp_set_socket_nonblocking_enable(g_ftp.server_sock);
            ftp_set_socket_reuseaddr_enable(g_ftp.server_sock);
            ftp_set_socket_nodelay_enable(g_ftp.server_sock);
            ftp_set_socket_keepalive_enable(g_ftp.server_sock);

            struct sockaddr_in sa = {
                .sin_family = PF_INET,
                .sin_port = htons(cfg->port),
                .sin_addr.s_addr = INADDR_ANY,
            };

            rc = socket_bind(g_ftp.server_sock, (struct sockaddr*)&sa, sizeof(sa));
            if (rc < 0) {
            } else {
                rc = socket_listen(g_ftp.server_sock, 5); /* SOMAXCONN */
            }
        }
    }

    return rc;
}

#if defined(HAVE_POLL) && HAVE_POLL
int ftpsrv_loop(int timeout_ms) {
    if (!g_ftp.initialised) {
        return FTP_API_LOOP_ERROR_INIT;
    }

    static struct pollfd fds[1 + FTP_MAX_SESSIONS * 2] = {0};
    const nfds_t nfds = FTP_ARR_SZ(fds);

    // initialise fds.
    for (size_t i = 0; i < nfds; i++) {
        fds[i].fd = -1;
        fds[i].revents = 0;
    }

    // add server socket to the first entry.
    fds[0].fd = g_ftp.server_sock;
    fds[0].events = POLLIN | POLLPRI;

    // add each session control and data socket.
    for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
        const size_t si = 1 + i * 2;
        const size_t sd = 1 + i * 2 + 1;
        const struct FtpSession* session = &g_ftp.sessions[i];

        if (session->active) {
            fds[si].fd = session->control_sock;
            fds[si].events = POLLIN | POLLPRI;

            if (session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                fds[sd].fd = session->data_sock;
                if (session->transfer.mode == FTP_TRANSFER_MODE_RETR) {
                    fds[sd].events = POLLOUT;
                } else {
                    fds[sd].events = POLLIN;
                }
            }
        }
    }

    const int rc = socket_poll(fds, nfds, timeout_ms);
    if (rc < 0) {
        return FTP_API_LOOP_ERROR_INIT;
    } else {
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return FTP_API_LOOP_ERROR_INIT;
        } else if (fds[0].revents & (POLLIN | POLLPRI)) {
            for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
                if (!g_ftp.sessions[i].active) {
                    ftp_session_init(&g_ftp.sessions[i]);
                    break;
                }
            }
        }

        for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
            const size_t si = 1 + i * 2;
            const size_t sd = 1 + i * 2 + 1;
            struct FtpSession* session = &g_ftp.sessions[i];

            if (fds[si].revents & (POLLERR | POLLHUP)) {
                ftp_session_close(session);
            } else if (fds[si].revents & (POLLIN | POLLPRI)) {
                ftp_session_poll(session);
            }

            // don't close data transfer on error as it will confuse the client (ffmpeg)
            if (session->active && session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (fds[sd].revents & (POLLIN | POLLOUT)) {
                    ftp_data_transfer_progress(session);
                }
            }
        }
    }

    return FTP_API_LOOP_ERROR_OK;
}
#else
int ftpsrv_loop(int timeout_ms) {
    if (!g_ftp.initialised) {
        return FTP_API_LOOP_ERROR_INIT;
    }

    // initialise fds.
    int nfds = 0;
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);

    // sets an fd for r/w and efds and adjusts nfds.
    #define FD_SET_HELPER(nfds, fd, rwsetp) do { \
        assert(fd < FD_SETSIZE && "fd is out of range!"); \
        nfds = fd > nfds ? fd : nfds; \
        FD_SET(fd, rwsetp); FD_SET(fd, &efds); \
    } while (0)

    // add server socket to the first entry.
    FD_SET_HELPER(nfds, g_ftp.server_sock, &rfds);

    // add each session control and data socket.
    for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
        const struct FtpSession* session = &g_ftp.sessions[i];

        if (session->active) {
            FD_SET_HELPER(nfds, session->control_sock, &rfds);
            if (session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (session->transfer.mode == FTP_TRANSFER_MODE_RETR) {
                    FD_SET_HELPER(nfds, session->data_sock, &wfds);
                } else {
                    FD_SET_HELPER(nfds, session->data_sock, &rfds);
                }
            }
        }
    }

    // if -1, then set tvp to NULL to wait forever.
    struct timeval tv;
    struct timeval* tvp = NULL;
    if (timeout_ms >= 0) {
        tvp = &tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    }

    const int rc = socket_select(nfds + 1, &rfds, &wfds, &efds, tvp);
    if (rc < 0) {
        return FTP_API_LOOP_ERROR_INIT;
    } else {
        if (FD_ISSET(g_ftp.server_sock, &efds)) {
            return FTP_API_LOOP_ERROR_INIT;
        } else if (FD_ISSET(g_ftp.server_sock, &rfds)) {
            for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
                if (!g_ftp.sessions[i].active) {
                    ftp_session_init(&g_ftp.sessions[i]);
                    break;
                }
            }
        }

        for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
            struct FtpSession* session = &g_ftp.sessions[i];

            if (FD_ISSET(session->control_sock, &efds)) {
                ftp_session_close(session);
            } else if (FD_ISSET(session->control_sock, &rfds)) {
                ftp_session_poll(session);
            }

            // don't close data transfer on error as it will confuse the client (ffmpeg)
            if (session->active && session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (FD_ISSET(session->data_sock, &rfds) || FD_ISSET(session->data_sock, &wfds)) {
                    ftp_data_transfer_progress(session);
                }
            }
        }
    }

    return FTP_API_LOOP_ERROR_OK;
}
#endif // defined(HAVE_POLL) && HAVE_POLL

void ftpsrv_exit(void) {
    if (!g_ftp.initialised) {
        return;
    }

    for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
        if (g_ftp.sessions[i].active) {
            ftp_session_close(&g_ftp.sessions[i]);
        }
    }

    ftp_close_socket(&g_ftp.server_sock);
    g_ftp.initialised = 0;
}
