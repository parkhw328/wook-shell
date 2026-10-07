#include "SSH.h"
#include <libssh2.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct WSSH {
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    pthread_mutex_t lock;
    int socket, cancelled;
    char error[256];
};
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void initialize(void) { libssh2_init(0); }
static int fail(WSSH *s, const char *message) {
    snprintf(s->error, sizeof(s->error), "%s", message); return -1;
}
static int result(WSSH *s, int code) {
    if (code < 0) {
        char *message = NULL;
        libssh2_session_last_error(s->session, &message, NULL, 0);
        fail(s, message ? message : "SSH operation failed");
    }
    return code;
}
WSSH *wssh_new(void) {
    pthread_once(&once, initialize);
    WSSH *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->socket = -1; pthread_mutex_init(&s->lock, NULL);
    s->session = libssh2_session_init();
    if (!s->session) { pthread_mutex_destroy(&s->lock); free(s); return NULL; }
    libssh2_session_set_timeout(s->session, 15000);
    libssh2_session_set_blocking(s->session, 1);
    return s;
}
void wssh_cancel(WSSH *s) {
    pthread_mutex_lock(&s->lock); s->cancelled = 1;
    if (s->socket >= 0) shutdown(s->socket, SHUT_RDWR);
    pthread_mutex_unlock(&s->lock);
}
void wssh_free(WSSH *s) {
    if (!s) return;
    wssh_cancel(s);
    libssh2_session_set_blocking(s->session, 0);
    if (s->channel) libssh2_channel_free(s->channel);
    libssh2_session_free(s->session);
    pthread_mutex_lock(&s->lock);
    if (s->socket >= 0) close(s->socket);
    s->socket = -1;
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_destroy(&s->lock); free(s);
}
const char *wssh_error(WSSH *s) { return s->error; }
int wssh_connect(WSSH *s, const char *host, int port) {
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    char service[8]; snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(host, service, &hints, &addresses)) return fail(s, "Cannot resolve host");
    int connected = 0;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        pthread_mutex_lock(&s->lock);
        if (s->cancelled) { pthread_mutex_unlock(&s->lock); break; }
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        s->socket = fd;
        pthread_mutex_unlock(&s->lock);
        if (fd < 0) continue;
        int yes = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc && errno == EINPROGRESS) {
            struct pollfd p = {fd, POLLOUT, 0};
            if (poll(&p, 1, 15000) > 0) {
                int error = 0; socklen_t length = sizeof(error);
                rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) || error;
            }
        }
        if (!rc) { connected = 1; break; }
        pthread_mutex_lock(&s->lock); close(fd); s->socket = -1; pthread_mutex_unlock(&s->lock);
    }
    freeaddrinfo(addresses);
    if (!connected) return fail(s, "Connection failed or timed out");
    return result(s, libssh2_session_handshake(s->session, s->socket));
}
const unsigned char *wssh_fingerprint(WSSH *s) {
    return (const unsigned char *)libssh2_hostkey_hash(s->session, LIBSSH2_HOSTKEY_HASH_SHA256);
}
int wssh_auth(WSSH *s, const char *user, const char *password,
              const char *key, size_t length, const char *passphrase) {
    int rc;
    if (key && length) {
        rc = libssh2_userauth_publickey_frommemory(s->session, user, strlen(user),
                                                  NULL, 0, key, length, passphrase);
    } else {
        rc = libssh2_userauth_password(s->session, user, password);
    }
    return result(s, rc);
}
int wssh_shell(WSSH *s, int columns, int rows) {
    s->channel = libssh2_channel_open_session(s->session);
    if (!s->channel) return result(s, -1);
    if (result(s, libssh2_channel_request_pty_ex(s->channel, "xterm-256color", 14,
                       NULL, 0, columns, rows, 0, 0)) < 0) return -1;
    if (result(s, libssh2_channel_shell(s->channel)) < 0) return -1;
    libssh2_session_set_blocking(s->session, 0); return 0;
}
int wssh_read(WSSH *s, void *buffer, int capacity) {
    int n = (int)libssh2_channel_read(s->channel, buffer, capacity);
    if (n == LIBSSH2_ERROR_EAGAIN) return 0;
    if (n < 0) return result(s, n);
    if (!n && libssh2_channel_eof(s->channel)) return -1;
    return n;
}
int wssh_write(WSSH *s, const void *buffer, int size) {
    int n = (int)libssh2_channel_write(s->channel, buffer, size);
    return n == LIBSSH2_ERROR_EAGAIN ? 0 : result(s, n);
}
int wssh_resize(WSSH *s, int columns, int rows) {
    int n = libssh2_channel_request_pty_size(s->channel, columns, rows);
    return n == LIBSSH2_ERROR_EAGAIN ? 1 : result(s, n);
}
static int ftp_read(void *context, void *buffer, int size) {
    WSSH *s = context; return (int)libssh2_channel_read(s->channel, buffer, size);
}
static int ftp_write(void *context, const void *buffer, int size) {
    WSSH *s = context; return (int)libssh2_channel_write(s->channel, buffer, size);
}
WsFtp *wssh_sftp(WSSH *s) {
    s->channel = libssh2_channel_open_session(s->session);
    if (!s->channel) { result(s, -1); return NULL; }
    if (result(s, libssh2_channel_subsystem(s->channel, "sftp")) < 0) return NULL;
    return wsftp_create(s, ftp_read, ftp_write);
}
