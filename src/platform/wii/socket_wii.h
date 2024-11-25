#include <network.h>
#include <errno.h>

#undef HAVE_POLL
#define HAVE_POLL 1

struct pollfd {
    s32 fd;
    u32 events;
    u32 revents;
};
typedef s32 nfds_t;

#define SHUT_RDWR 2

// the below wraps around the net_ functions and correctly
// sets errno on error.
static inline s32 socket_open(u32 domain,u32 type,u32 protocol) {
    const int result = net_socket(domain, type, protocol);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_recv(s32 s,void *mem,s32 size,u32 flags) {
    const int result = net_recv(s, mem, size, flags);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_send(s32 s,const void *data,s32 size,u32 flags) {
    const int result = net_send(s, data, size, flags);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_close(s32 s) {
    const int result = net_close(s);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_shutdown(s32 s, u32 how) {
    const int result = net_shutdown(s, how);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_accept(s32 s,struct sockaddr *addr,socklen_t *addrlen) {
    const int result = net_accept(s, addr, addrlen);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_bind(s32 s,struct sockaddr *name,socklen_t namelen) {
    const int result = net_bind(s, name, namelen);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_connect(s32 s,struct sockaddr *addr,socklen_t addrlen) {
    const int result = net_connect(s, addr, addrlen);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_listen(s32 s,u32 backlog) {
    const int result = net_listen(s, backlog);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_getsockname(s32 s, struct sockaddr *addr, socklen_t *addrlen) {
    const int result = net_getsockname(s, addr, addrlen);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_setsockopt(s32 s,u32 level,u32 optname,const void *optval,socklen_t optlen) {
    const int result = net_setsockopt(s, level, optname, optval, optlen);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_fcntl(s32 s, u32 cmd, u32 flags) {
    const int result = net_fcntl(s, cmd, flags);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

static inline s32 socket_poll(struct pollfd *fds, s32 nsds, s32 timeout) {
    const int result = net_poll((struct pollsd*)fds, nsds, timeout);
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}
