/**
 * Copyright 2024 TotalJustice.
 * SPDX-License-Identifier: MIT
 */
#include "ftpsrv.h"
#include "ftpsrv_vfs.h"
#include "ftpsrv_socket.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
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
    FTP_TRANSFER_MODE_RETR, // transfer using RETR
    FTP_TRANSFER_MODE_STOR, // transfer using STOR
    FTP_TRANSFER_MODE_LIST, // transfer using LIST
    FTP_TRANSFER_MODE_NLST, // transfer using NLST
};

enum FTP_AUTH_MODE {
    FTP_AUTH_MODE_NONE,      // not authenticated
    FTP_AUTH_MODE_NEED_PASS, // username ok, waiting for password
    FTP_AUTH_MODE_VALID,     // authenticated
};

struct Pathname {
    char s[FTP_PATHNAME_SIZE];
};

struct FtpTransfer {
    enum FTP_TRANSFER_MODE mode;
    bool connection_pending;

    size_t offset;
    size_t size; // only set during RETR, LIST and NLIST.
    size_t index; // only used for NLIST and LIST devices.

    struct FtpVfsFile file_vfs;
    struct FtpVfsDir dir_vfs;

    char list_buf[1024];
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

    long server_marker; // file offset when using REST

    char cmd_buf[1024];
    size_t cmd_buf_size;

    char send_buf[1024];
    size_t send_buf_offset;
    size_t send_buf_size;

    struct Pathname pwd;   // current directory
    struct Pathname temp_path; // rename from buffer / LIST fullpath
};

struct FtpCommand {
    const char* name;
    void (*func)(struct FtpSession* session, const char* data);
    bool auth_required;
    bool args_required;
    bool data_connection_required;
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

static void ftp_set_server_socket_options(int sock) {
    ftp_set_socket_nonblocking_enable(sock);
    ftp_set_socket_reuseaddr_enable(sock);
    ftp_set_socket_nodelay_enable(sock);
    ftp_set_socket_keepalive_enable(sock);
}

static void ftp_set_data_socket_options(int sock) {
    ftp_set_socket_nonblocking_enable(sock);
    ftp_set_socket_keepalive_enable(sock);
    ftp_set_socket_throughput_enable(sock);
}

static inline unsigned socket_bind_port(void) {
    static unsigned port = 49152;
    const unsigned ret = port;
    port = (port == 65535) ? 49152 : port + 1;
    return ret;
}

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
    if (rc < 0 || rc >= sizeof(*out)) {
        errno = ENAMETOOLONG;
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
static int ftp_build_list_entry(struct FtpSession* session, const time_t cur_time, const struct Pathname* fullpath, const char* name, const struct stat* st) {
    int rc;
    struct FtpTransfer* transfer = &session->transfer;

    if (transfer->mode == FTP_TRANSFER_MODE_NLST) {
        rc = snprintf(transfer->list_buf, sizeof(transfer->list_buf), "%s" TELNET_EOL, name);
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

        rc = snprintf(transfer->list_buf, sizeof(transfer->list_buf), "%s %3u %s %s %13zu %s %3d %s %s%s" TELNET_EOL,
            perms,
            nlink,
            ftp_vfs_getpwuid(st), ftp_vfs_getgrgid(st),
            size,
            months[tm.tm_mon], tm.tm_mday, date,
            name, symlink_path.s);
    }

    if (rc <= 0 || rc > sizeof(transfer->list_buf)) {
        // don't send anything on error or truncated
        errno = ENAMETOOLONG;
        rc = -1;
    } else {
        transfer->size = rc;
    }

    return rc;
}

static void ftp_client_msg(struct FtpSession* session, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vsnprintf(session->send_buf, sizeof(session->send_buf) - 2, fmt, va);
    va_end(va);

    const int code = atoi(session->send_buf);
    if (code < 400) {
        ftp_log_callback(FTP_API_LOG_TYPE_RESPONSE, session->send_buf);
    } else {
        ftp_log_callback(FTP_API_LOG_TYPE_ERROR, session->send_buf);
    }

    strcat(session->send_buf, TELNET_EOL);
    session->send_buf_offset = 0;
    session->send_buf_size = strlen(session->send_buf);
}

// https://blog.netherlabs.nl/articles/2009/01/18/the-ultimate-so_linger-page-or-why-is-my-tcp-not-reliable
static void ftp_close_socket(int* sock) {
    // tldr: when send() returns, this does not mean that all data
    // has been sent to the server. All this means is that the data
    // has been sent to the kernel!
    // when calling close, the kernel *may* remove that data, and
    // thus the client will *not* receive it!
    if (*sock >= 0) {
        socket_shutdown(*sock, SHUT_RDWR);
        socket_close(*sock);
        *sock = -1;
    }
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
    ftp_vfs_closedir(&session->transfer.dir_vfs);

    session->transfer.connection_pending = false;
    session->temp_path.s[0] = '\0';
    session->transfer.offset = 0;
    session->transfer.size = 0;
    session->transfer.mode = FTP_TRANSFER_MODE_NONE;
    session->data_connection = FTP_DATA_CONNECTION_NONE;
}

static void ftp_data_open(struct FtpSession* session, enum FTP_TRANSFER_MODE mode) {
    int rc = 0;
    ftp_client_msg(session, "150 File status okay; about to open data connection.");

    if (session->data_connection == FTP_DATA_CONNECTION_ACTIVE) {
        rc = session->data_sock = socket_open(PF_INET, SOCK_STREAM, 0);
        if (rc >= 0) {
            ftp_set_data_socket_options(session->data_sock);
        }
    }

    if (rc < 0) {
        ftp_client_msg(session, "425 Can't open data connection [NORM], %s.", strerror(errno));
        ftp_data_transfer_end(session);
    } else {
        session->transfer.mode = mode;
        session->transfer.index = 0;
        session->transfer.connection_pending = true;
    }
}

static void ftp_data_poll(struct FtpSession* session) {
    int rc = 0;

    if (session->data_connection == FTP_DATA_CONNECTION_ACTIVE) {
        rc = socket_connect(session->data_sock, (struct sockaddr*)&session->data_sockaddr, sizeof(session->data_sockaddr));
    } else {
        socklen_t socklen = sizeof(session->pasv_sockaddr);
        rc = session->data_sock = socket_accept(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, &socklen);
        if (rc >= 0) {
            ftp_set_data_socket_options(session->data_sock);
        }
    }

    if (rc < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINPROGRESS) {
            ftp_client_msg(session, "425 Can't open data connection, [poll] %s.", strerror(errno));
            ftp_data_transfer_end(session);
        }
    } else {
        session->transfer.connection_pending = false;
    }
}

static void ftp_dir_data_transfer_progress(struct FtpSession* session) {
    const time_t cur_time = time(NULL);
    const bool device_list = g_ftp.cfg.devices && g_ftp.cfg.devices_count && !strcmp("/", session->temp_path.s);
    const bool is_root = !strcmp("/", session->temp_path.s);
    struct FtpTransfer* transfer = &session->transfer;

    // send as much data as possible.
    while (1) {
        if (transfer->size) {
            errno = 0;
            const int n = socket_send(session->data_sock, transfer->list_buf + transfer->offset, transfer->size, 0);
            if (n < 0) {
                // check if it failed due to anything but blocking.
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    ftp_client_msg(session, "426 bad Connection closed; transfer aborted, %s.", strerror(errno));
                    ftp_data_transfer_end(session);
                }
                break;
            } else if (n != transfer->size) {
                // partial transfer.
                transfer->offset += n;
                transfer->size -= n;
                break;
            } else {
                transfer->list_buf[0] = '\0';
                transfer->offset = 0;
                transfer->size = 0;

                // check if we are finished with this transfer.
                if (!ftp_vfs_isdir_open(&transfer->dir_vfs)) {
                    if (!device_list || transfer->index == g_ftp.cfg.devices_count) {
                        ftp_client_msg(session, "226 Closing data connection.");
                        ftp_data_transfer_end(session);
                        break;
                    }
                }
            }
        } else {
            // parse the next file.
            if (device_list) {
                struct stat st = {0};
                st.st_nlink = 1;
                st.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
                ftp_build_list_entry(session, cur_time, NULL, g_ftp.cfg.devices[transfer->index++].mount, &st);
            } else {
                struct FtpVfsDirEntry entry;
                const char* name = ftp_vfs_readdir(&transfer->dir_vfs, &entry);
                if (!name) {
                    ftp_client_msg(session, "226 Closing data connection.");
                    ftp_data_transfer_end(session);
                    break;
                }

                if (!strcmp(".", name) || !strcmp("..", name)) {
                    continue;
                }

                int rc;
                struct Pathname filepath;
                if (is_root) {
                    rc = snprintf(filepath.s, sizeof(filepath), "%s%s", session->temp_path.s, name);
                } else {
                    rc = snprintf(filepath.s, sizeof(filepath), "%s/%s", session->temp_path.s, name);
                }

                if (rc <= 0 || rc >= sizeof(filepath)) {
                    continue;
                }

                struct stat st = {0};
                rc = ftp_vfs_dirlstat(&transfer->dir_vfs, &entry, filepath.s, &st);
                if (rc < 0) {
                    continue;
                }

                ftp_build_list_entry(session, cur_time, &filepath, name, &st);
            }
        }
    }
}

static void ftp_file_data_transfer_progress(struct FtpSession* session) {
    int n = 0;
    errno = 0;
    struct FtpTransfer* transfer = &session->transfer;

    if (transfer->mode == FTP_TRANSFER_MODE_RETR) {
        #if defined(FTP_VFS_FD) && defined(HAVE_SENDFILE) && HAVE_SENDFILE
        n = sendfile(session->data_sock, transfer->file_vfs.fd, NULL, transfer->size - transfer->offset);
        if (n < 0 && (errno == EINVAL || errno == ENOSYS))
        #endif
        {
            const int read = n = ftp_vfs_read(&transfer->file_vfs, g_ftp.data_buf, sizeof(g_ftp.data_buf));
            if (n > 0) {
                n = socket_send(session->data_sock, g_ftp.data_buf, n, 0);
                if (n >= 0 && n != read) {
                    ftp_vfs_seek(&transfer->file_vfs, transfer->offset + (size_t)n);
                }
            } else {
                ftp_log_callback(FTP_API_LOG_TYPE_ERROR, "vfs read failed");
            }
        }
    } else {
        n = socket_recv(session->data_sock, g_ftp.data_buf, sizeof(g_ftp.data_buf), 0);
        if (n > 0) {
            n = ftp_vfs_write(&transfer->file_vfs, g_ftp.data_buf, n);
        }
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

static void ftp_data_transfer_progress(struct FtpSession* session) {
    const struct FtpTransfer* transfer = &session->transfer;
    if (transfer->mode) {
        if (transfer->mode == FTP_TRANSFER_MODE_RETR || transfer->mode == FTP_TRANSFER_MODE_STOR) {
            ftp_file_data_transfer_progress(session);
        } else {
            ftp_dir_data_transfer_progress(session);
        }
    }
}

// USER <SP> <username> <CRLF> | 230, 530, 500, 501, 421, 331, 332
static void ftp_cmd_USER(struct FtpSession* session, const char* data) {
    char username[128] = {0};
    int rc = snprintf(username, sizeof(username), "%s", data);

    if (rc <= 0 || rc >= sizeof(username)) {
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
    int rc = snprintf(password, sizeof(password), "%s", data);

    if (rc <= 0 || rc >= sizeof(password)) {
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
        ftp_client_msg(session, "550 Requested action not taken, %s. Bad path: %s.", strerror(errno), fullpath.s);
    } else {
        session->pwd = fullpath;
        ftp_client_msg(session, "200 Command okay.");
    }
}

// CWD <SP> <pathname> <CRLF> | 250, 500, 501, 502, 421, 530, 550
static void ftp_cmd_CWD(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
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

    unsigned char h[6] = {0}; // ip addr / port
    for (int i = 0; i < 6; i++) {
        char* end_ptr;
        const unsigned long value = strtoul(data, &end_ptr, 10);
        if ((!value && data == end_ptr) || value > 255) {
            ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
            return;
        }

        h[i] = value;

        // skip over comma.
        if (end_ptr[0] != '\0') {
            data = end_ptr + 1;
        }
    }

    // convert ip string to sockaddr_in
    char ip_buf[16] = {0};
    int rc = snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", h[0], h[1], h[2], h[3]);
    if (rc <= 0 || rc >= sizeof(ip_buf)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments, %s.", strerror(errno));
    } else {
        rc = inet_aton(ip_buf, &session->data_sockaddr.sin_addr);
        if (rc < 0) {
            ftp_client_msg(session, "501 Syntax error in parameters or arguments, %s.", strerror(errno));
        } else {
            session->data_sockaddr.sin_family = PF_INET;
            session->data_sockaddr.sin_port = htons((h[4] << 8) + h[5]);
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
        ftp_set_server_socket_options(session->pasv_sock);

        // copy current over ip addr
        session->pasv_sockaddr = session->control_sockaddr;
        session->pasv_sockaddr.sin_port = htons(socket_bind_port());

        rc = socket_bind(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, sizeof(session->pasv_sockaddr));
        if (rc < 0) {
            ftp_client_msg(session, "501 bind failed Syntax error in parameters or arguments. %d", rc);
        } else {
            rc = socket_listen(session->pasv_sock, 1);
            if (rc < 0) {
                ftp_client_msg(session, "501 listen failed Syntax error in parameters or arguments, %s.", strerror(errno));
            } else {
                socklen_t base_len = sizeof(session->pasv_sockaddr);
                rc = socket_getsockname(session->pasv_sock, (struct sockaddr*)&session->pasv_sockaddr, &base_len);
                if (rc < 0) {
                    ftp_client_msg(session, "501 socket_getsockname failed Syntax error in parameters or arguments, %s.", strerror(errno));
                } else {
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
        }
        ftp_close_socket(&session->pasv_sock);
    }
}

// TYPE <SP> <type-code> <CRLF> | 200, 500, 501, 504, 421, 530
static void ftp_cmd_TYPE(struct FtpSession* session, const char* data) {
    const char code = data[0];

    if (code == '\0') {
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
    const char code = data[0];

    if (code == '\0') {
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
    const char code = data[0];

    if (code == '\0') {
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
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken.");
        } else {
            rc = ftp_vfs_open(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, FtpVfsOpenMode_READ);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken, %s Failed to open path: %s.", strerror(errno), fullpath.s);
            } else {
                struct stat st = {0};
                rc = ftp_vfs_fstat(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, &st);
                if (rc < 0) {
                    ftp_client_msg(session, "550 Requested action not taken, %s. Failed to fstat path: %s", strerror(errno), fullpath.s);
                } else {
                    session->transfer.offset = 0;
                    session->transfer.size = st.st_size;

                    if (session->server_marker > 0) {
                        session->transfer.offset = session->server_marker;
                        rc = ftp_vfs_seek(&session->transfer.file_vfs, session->transfer.offset);
                        session->server_marker = 0;
                    }

                    if (rc < 0) {
                        ftp_client_msg(session, "550 Requested action not taken, %s. Failed to fseek path: %s", strerror(errno), fullpath.s);
                    } else {
                        ftp_data_open(session, FTP_TRANSFER_MODE_RETR);
                    }
                }
            }
        }
    }
}

// STOR <SP> <pathname> <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 551, 552, 532, 450, 452, 553, 500, 501, 421, 530
static void ftp_cmd_STOR(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
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
            ftp_client_msg(session, "551 Requested action aborted: page type unknown, %s.", strerror(errno));
        } else {
            rc = ftp_vfs_open(&session->transfer.file_vfs, fix_path_for_device(&fullpath).s, flags);
            if (rc < 0) {
                ftp_client_msg(session, "551 Requested action aborted: page type unknown, %s. Failed to open path: %s", strerror(errno), fullpath.s);
            } else {
                ftp_data_open(session, FTP_TRANSFER_MODE_STOR);
            }
        }
    }
}

#if 0
// STOU <CRLF> | 125, 150, (110), 226, 250, 425, 426, 451, 551, 552, 532, 450, 452, 553, 500, 501, 421, 530
static void ftp_cmd_STOU(struct FtpSession* session, const char* data) {
    struct Pathname unique_name = {"unique_file_XXXXXX"};
    if (!mktemp(unique_name.s)) {
        ftp_client_msg(session, "553 Requested action not taken, %s.", strerror(errno));
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
    char* end_ptr;
    const long server_marker = strtol(data, &end_ptr, 10);

    if ((!server_marker && data == end_ptr) || server_marker < 0) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        session->server_marker = server_marker;
        ftp_client_msg(session, "350 Requested file action pending further information.");
    }
}

// RNFR <SP> <pathname> <CRLF> | 450, 550, 500, 501, 502, 421, 530, 350
static void ftp_cmd_RNFR(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        rc = build_fullpath(session, &session->temp_path, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken, %s.", strerror(errno));
        } else {
            ftp_client_msg(session, "350 Requested file action pending further information.");
        }
    }
}

// RNTO <SP> <pathname> <CRLF> | 250, 532, 553, 500, 501, 502, 503, 421, 530
static void ftp_cmd_RNTO(struct FtpSession* session, const char* data) {
    struct Pathname pathname = {0};
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        if (session->temp_path.s[0] == '\0') {
            ftp_client_msg(session, "503 Bad sequence of commands.");
        } else {
            struct Pathname dst_path;
            rc = build_fullpath(session, &dst_path, pathname);
            if (rc < 0) {
                ftp_client_msg(session, "553 Requested action not taken, %s.", strerror(errno));
            } else {
                rc = ftp_vfs_rename(session->temp_path.s, dst_path.s);
                if (rc < 0) {
                    ftp_client_msg(session, "553 Requested action not taken, %s.", strerror(errno));
                } else {
                    ftp_client_msg(session, "250 Requested file action okay, completed.");
                }
            }
        }
    }

    session->temp_path.s[0] = '\0';
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
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken, %s.", strerror(errno));
        } else {
            rc = func(fix_path_for_device(&fullpath).s);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken, %s.", strerror(errno));
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
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "550 Requested action not taken, %s.", strerror(errno));
        } else {
            rc = ftp_vfs_mkdir(fix_path_for_device(&fullpath).s);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken, %s.", strerror(errno));
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
static void ftp_list_directory(struct FtpSession* session, const char* data, enum FTP_TRANSFER_MODE mode) {
    struct Pathname pathname = {0};
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    // see issue: #2
    if (rc == 0 || !strcmp("-a", pathname.s) || !strcmp("-la", pathname.s)) {
        session->temp_path = session->pwd;
    } else {
        rc = build_fullpath(session, &session->temp_path, pathname);
    }

    if (rc < 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        // check if on root and using devices
        if (g_ftp.cfg.devices && g_ftp.cfg.devices_count && !strcmp("/", session->temp_path.s)) {
            session->transfer.index = 0;
            ftp_data_open(session, mode);
        } else {
            session->temp_path = fix_path_for_device(&session->temp_path);
            struct stat st = {0};
            rc = ftp_vfs_lstat(session->temp_path.s, &st);
            if (rc < 0) {
                ftp_client_msg(session, "450 Requested file action not taken. %s. Failed to stat path: %s.", strerror(errno), session->temp_path.s);
            } else {
                if (S_ISDIR(st.st_mode)) {
                    rc = ftp_vfs_opendir(&session->transfer.dir_vfs, session->temp_path.s);
                    if (rc < 0) {
                        ftp_client_msg(session, "450 Requested file action not taken. %s. Failed to open dir: %s.", strerror(errno), session->temp_path.s);
                    } else {
                        ftp_data_open(session, mode);
                    }
                } else if (mode == FTP_TRANSFER_MODE_LIST) {
                    const time_t cur_time = time(NULL);
                    rc = ftp_build_list_entry(session, cur_time, &session->temp_path, pathname.s, &st);
                    if (rc < 0) {
                        ftp_client_msg(session, "450 Requested file action not taken, %s. Failed to build entry: %s.", strerror(errno), session->temp_path.s);
                    } else {
                        ftp_data_open(session, mode);
                    }
                } else {
                    ftp_client_msg(session, "450 Requested file action not taken. Nlist on file is not valid.");
                }
            }
        }
    }
}

// LIST [<SP> <pathname>] <CRLF> | 125, 150, 226, 250, 425, 426, 451, 450, 500, 501, 502, 421, 530
static void ftp_cmd_LIST(struct FtpSession* session, const char* data) {
    ftp_list_directory(session, data, FTP_TRANSFER_MODE_LIST);
}

// NLST [<SP> <pathname>] <CRLF> | 125, 150, 226, 250, 425, 426, 451, 450, 500, 501, 502, 421, 530
static void ftp_cmd_NLST(struct FtpSession* session, const char* data) {
    ftp_list_directory(session, data, FTP_TRANSFER_MODE_NLST);
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
    ftp_client_msg(session, "214 ftpsrv 0.2.0 By TotalJustice.");
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
    int rc = snprintf(pathname.s, sizeof(pathname), "%s", data);

    if (rc <= 0 || rc >= sizeof(pathname)) {
        ftp_client_msg(session, "501 Syntax error in parameters or arguments.");
    } else {
        struct Pathname fullpath = {0};
        rc = build_fullpath(session, &fullpath, pathname);
        if (rc < 0) {
            ftp_client_msg(session, "501 Syntax error in parameters or arguments, %s.", strerror(errno));
        } else {
            struct stat st = {0};
            rc = ftp_vfs_stat(fix_path_for_device(&fullpath).s, &st);
            if (rc < 0) {
                ftp_client_msg(session, "550 Requested action not taken, %s. Bad path: %s.", strerror(errno), fullpath.s);
            } else {
                ftp_client_msg(session, "213 %d", st.st_size);
            }
        }
    }
}

static const struct FtpCommand FTP_COMMANDS[] = {
    // ACCESS CONTROL COMMANDS: https://datatracker.ietf.org/doc/html/rfc959#section-4
    { .name = "USER", .func = ftp_cmd_USER, .auth_required = 0, .args_required = 1, .data_connection_required = 0 },
    { .name = "PASS", .func = ftp_cmd_PASS, .auth_required = 0, .args_required = 1, .data_connection_required = 0 },
    { .name = "ACCT", .func = ftp_cmd_ACCT, .auth_required = 0, .args_required = 1, .data_connection_required = 0 },
    { .name = "CWD",  .func = ftp_cmd_CWD,  .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "CDUP", .func = ftp_cmd_CDUP, .auth_required = 1, .args_required = 0, .data_connection_required = 0 },
    { .name = "SMNT", .func = ftp_cmd_SMNT, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "REIN", .func = ftp_cmd_REIN, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },
    { .name = "QUIT", .func = ftp_cmd_QUIT, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },

    // TRANSFER PARAMETER COMMANDS
    { .name = "PORT", .func = ftp_cmd_PORT, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "PASV", .func = ftp_cmd_PASV, .auth_required = 1, .args_required = 0, .data_connection_required = 0 },
    { .name = "TYPE", .func = ftp_cmd_TYPE, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "STRU", .func = ftp_cmd_STRU, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "MODE", .func = ftp_cmd_MODE, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },

    // FTP SERVICE COMMANDS
    { .name = "RETR", .func = ftp_cmd_RETR, .auth_required = 1, .args_required = 1, .data_connection_required = 1 },
    { .name = "STOR", .func = ftp_cmd_STOR, .auth_required = 1, .args_required = 1, .data_connection_required = 1 },
    // { .name = "STOU",  .func = ftp_cmd_STOU, .auth_required = 1, .args_required = 0, .data_connection_required = 0 },
    { .name = "APPE", .func = ftp_cmd_APPE, .auth_required = 1, .args_required = 1, .data_connection_required = 1 },
    { .name = "ALLO", .func = ftp_cmd_ALLO, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "REST", .func = ftp_cmd_REST, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "RNFR", .func = ftp_cmd_RNFR, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "RNTO", .func = ftp_cmd_RNTO, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "ABOR", .func = ftp_cmd_ABOR, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },
    { .name = "DELE", .func = ftp_cmd_DELE, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "RMD",  .func = ftp_cmd_RMD,  .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "MKD",  .func = ftp_cmd_MKD,  .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "PWD",  .func = ftp_cmd_PWD,  .auth_required = 1, .args_required = 0, .data_connection_required = 0 },
    { .name = "LIST", .func = ftp_cmd_LIST, .auth_required = 1, .args_required = 0, .data_connection_required = 1 },
    { .name = "NLST", .func = ftp_cmd_NLST, .auth_required = 1, .args_required = 0, .data_connection_required = 1 },
    { .name = "SITE", .func = ftp_cmd_SITE, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
    { .name = "SYST", .func = ftp_cmd_SYST, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },
    { .name = "STAT", .func = ftp_cmd_STAT, .auth_required = 1, .args_required = 0, .data_connection_required = 0 },
    { .name = "HELP", .func = ftp_cmd_HELP, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },
    { .name = "NOOP", .func = ftp_cmd_NOOP, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },

    // extensions
    { .name = "FEAT", .func = ftp_cmd_FEAT, .auth_required = 0, .args_required = 0, .data_connection_required = 0 },
    { .name = "SIZE", .func = ftp_cmd_SIZE, .auth_required = 1, .args_required = 1, .data_connection_required = 0 },
};

static int ftp_session_init(struct FtpSession* session) {
    struct sockaddr_in sa;
    socklen_t addr_len = sizeof(sa);
    int rc = 0;
    errno = 0;

    int control_sock = rc = socket_accept(g_ftp.server_sock, (struct sockaddr*)&sa, &addr_len);
    if (rc < 0) {
        return rc;
    } else {
        ftp_set_server_socket_options(control_sock);

        memset(session, 0, sizeof(*session));
        session->control_sock = control_sock;
        session->control_sockaddr = sa;
        addr_len = sizeof(session->control_sockaddr);

        rc = socket_getsockname(session->control_sock, (struct sockaddr*)&session->control_sockaddr, &addr_len);
        if (rc < 0) {
            ftp_client_msg(session, "451 Failed to get connection info, %s.", strerror(errno));
        } else {
            session->active = 1;
            session->data_sock = -1;
            session->pasv_sock = -1;
            session->data_connection = FTP_DATA_CONNECTION_NONE;
            strcpy(session->pwd.s, "/");
            g_ftp.session_count++;
            ftp_client_msg(session, "220 Service ready for new user.");
            return 0;
        }
        ftp_close_socket(&control_sock);
    }
    return rc;
}

static void ftp_session_close(struct FtpSession* session) {
    if (session->active) {
        ftp_close_socket(&session->control_sock);
        ftp_data_transfer_end(session);
        memset(session, 0, sizeof(*session));
        g_ftp.session_count--;
    }
}

static void ftp_session_progress_line(struct FtpSession* session, const char* line, size_t line_len) {
    char cmd_name[5] = {0};
    int rc = snprintf(cmd_name, sizeof(cmd_name), "%s", line);
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
            ftp_client_msg(session, "500 Syntax error, command \"%s\" unrecognized.", cmd_name);
        } else {
            const struct FtpCommand* cmd = &FTP_COMMANDS[command_id];
            const char* cmd_args = memchr(line + strlen(cmd->name), ' ', line_len - strlen(cmd->name));

            // validate the command
            if (!cmd->args_required && cmd_args) {
                ftp_client_msg(session, "501 Syntax error in parameters or arguments, extra args.");
            } else if (cmd->args_required && !cmd_args) {
                ftp_client_msg(session, "501 Syntax error in parameters or arguments, missing required args.");
            } else if (cmd->auth_required && session->auth_mode != FTP_AUTH_MODE_VALID) {
                ftp_client_msg(session, "530 Not logged in.");
            } else if (cmd->data_connection_required && session->data_connection == FTP_DATA_CONNECTION_NONE) {
                ftp_client_msg(session, "501 Syntax error in parameters or arguments, no data connection.");
            } else {
                const char* args = cmd_args ? cmd_args + 1 : "\0";
                cmd->func(session, args);
            }
        }
    }
}

static void ftp_session_send(struct FtpSession* session) {
    int rc = socket_send(session->control_sock, session->send_buf + session->send_buf_offset, session->send_buf_size - session->send_buf_offset, 0);
    if (rc < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            ftp_session_close(session);
        }
    } else {
        session->send_buf_offset += rc;
        session->send_buf_size -= rc;
    }
}

static void ftp_session_poll(struct FtpSession* session) {
    int rc = socket_recv(session->control_sock, session->cmd_buf + session->cmd_buf_size, sizeof(session->cmd_buf) - session->cmd_buf_size, 0);
    if (rc < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            ftp_session_close(session);
        }
    } else if (rc == 0) {
        ftp_session_close(session);
    } else {
        session->cmd_buf_size += rc;

        do {
            size_t line_len = 0;
            for (size_t i = 0; i < session->cmd_buf_size - 1; i++) {
                if (!memcmp(session->cmd_buf + i, TELNET_EOL, strlen(TELNET_EOL))) {
                    // replace TELNET_EOL with NULL as to terminate the string.
                    session->cmd_buf[i] = '\0';
                    line_len = i + strlen(TELNET_EOL);
                    break;
                }
            }

            if (!line_len) {
                // no room for TELNET_EOL, so reset the buffer.
                if (session->cmd_buf_size == sizeof(session->cmd_buf)) {
                    session->cmd_buf_size = 0;
                }
                break;
            }

            ftp_session_progress_line(session, session->cmd_buf, line_len);
            memcpy(session->cmd_buf, session->cmd_buf + line_len, session->cmd_buf_size - line_len);
            session->cmd_buf_size -= line_len;
        } while (session->cmd_buf_size);
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
            ftp_set_server_socket_options(g_ftp.server_sock);

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
    }

    // add server socket to the first entry.
    if (g_ftp.session_count < FTP_MAX_SESSIONS) {
        fds[0].fd = g_ftp.server_sock;
        fds[0].events = POLLIN;
    }

    // add each session control and data socket.
    for (size_t i = 0; i < FTP_ARR_SZ(g_ftp.sessions); i++) {
        const size_t si = 1 + i * 2;
        const size_t sd = 1 + i * 2 + 1;
        const struct FtpSession* session = &g_ftp.sessions[i];

        if (session->active) {
            fds[si].fd = session->control_sock;
            if (session->send_buf_size) {
                fds[si].events = POLLOUT;
            } else {
                fds[si].events = POLLIN;
            }

            if (session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                // wait until the socket is ready to connect.
                if (session->transfer.connection_pending) {
                    fds[sd].events = POLLIN;
                    if (session->data_connection == FTP_DATA_CONNECTION_PASSIVE) {
                        fds[sd].fd = session->pasv_sock;
                        fds[sd].events = POLLIN;
                    } else {
                        fds[sd].fd = session->data_sock;
                        fds[sd].events = POLLOUT;
                    }
                } else {
                    fds[sd].fd = session->data_sock;
                    if (session->transfer.mode == FTP_TRANSFER_MODE_STOR) {
                        fds[sd].events = POLLIN;
                    } else {
                        fds[sd].events = POLLOUT;
                    }
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
        } else if (fds[0].revents & POLLIN) {
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

            if (fds[si].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                ftp_session_close(session);
            } else if (fds[si].revents & POLLIN) {
                ftp_session_poll(session);
            } else if (fds[si].revents & POLLOUT) {
                ftp_session_send(session);
            }

            // don't close data transfer on error as it will confuse the client (ffmpeg)
            if (session->active && session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (fds[sd].revents & (POLLIN | POLLOUT)) {
                    if (session->transfer.connection_pending) {
                        ftp_data_poll(session);
                    } else {
                        ftp_data_transfer_progress(session);
                    }
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
            if (session->send_buf_size) {
                FD_SET_HELPER(nfds, session->control_sock, &wfds);
            } else {
                FD_SET_HELPER(nfds, session->control_sock, &rfds);
            }

            if (session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (session->transfer.connection_pending) {
                    if (session->data_connection == FTP_DATA_CONNECTION_PASSIVE) {
                        FD_SET_HELPER(nfds, session->pasv_sock, &rfds);
                    } else {
                        FD_SET_HELPER(nfds, session->data_sock, &wfds);
                    }
                } else {
                    if (session->transfer.mode == FTP_TRANSFER_MODE_STOR) {
                        FD_SET_HELPER(nfds, session->data_sock, &rfds);
                    } else {
                        FD_SET_HELPER(nfds, session->data_sock, &wfds);
                    }
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
            } else if (FD_ISSET(session->control_sock, &wfds)) {
                ftp_session_send(session);
            }

            // don't close data transfer on error as it will confuse the client (ffmpeg)
            if (session->active && session->transfer.mode != FTP_TRANSFER_MODE_NONE) {
                if (FD_ISSET(session->data_sock, &rfds) || FD_ISSET(session->data_sock, &wfds)) {
                    if (session->transfer.connection_pending) {
                        ftp_data_poll(session);
                    } else {
                        ftp_data_transfer_progress(session);
                    }
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
