#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#if defined(HAVE_POLL) && HAVE_POLL
    #include <poll.h>
#else
    #include <sys/select.h>
#endif

#define socket_open socket
#define socket_recv recv
#define socket_send send
#define socket_close close
#define socket_shutdown shutdown
#define socket_accept accept
#define socket_bind bind
#define socket_connect connect
#define socket_listen listen
#define socket_getsockname getsockname
#define socket_setsockopt setsockopt
#define socket_fcntl fcntl
#define socket_select select
#define socket_poll poll
