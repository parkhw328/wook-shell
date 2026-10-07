#pragma once
#include <stddef.h>
#include "wsftp.h"
typedef struct WSSH WSSH;
WSSH *wssh_new(void);
void wssh_cancel(WSSH *s);
void wssh_free(WSSH *s);
const char *wssh_error(WSSH *s);
int wssh_connect(WSSH *s, const char *host, int port);
const unsigned char *wssh_fingerprint(WSSH *s);
int wssh_auth(WSSH *s, const char *user, const char *password,
              const char *key, size_t length, const char *passphrase);
int wssh_shell(WSSH *s, int columns, int rows);
int wssh_read(WSSH *s, void *buffer, int capacity);
int wssh_write(WSSH *s, const void *buffer, int size);
int wssh_resize(WSSH *s, int columns, int rows);
WsFtp *wssh_sftp(WSSH *s);
