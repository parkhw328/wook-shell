#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Shared Windows/macOS SFTP v3 codec. SSH, credentials and host trust belong to
 * the platform transport; this component never implements cryptography. */
typedef struct WsFtp WsFtp;
typedef int (*WsFtpRead)(void *, void *, int);
typedef int (*WsFtpWrite)(void *, const void *, int);
typedef int (*WsFtpProgress)(void *, uint64_t, uint64_t);
typedef struct WsFtpAttrs { uint64_t size; uint32_t flags, permissions, modified; } WsFtpAttrs;
typedef int (*WsFtpEntry)(void *, const char *, const WsFtpAttrs *);
WsFtp *wsftp_create(void *context, WsFtpRead read, WsFtpWrite write);
void wsftp_free(WsFtp *client);
const char *wsftp_error(WsFtp *client);
int wsftp_status(WsFtp *client);
int wsftp_init(WsFtp *client);
int wsftp_realpath(WsFtp *client, const char *path, char *output, size_t capacity);
int wsftp_list(WsFtp *client, const char *path, WsFtpEntry entry, void *context);
int wsftp_stat(WsFtp *client, const char *path, WsFtpAttrs *attrs);
int wsftp_mkdir(WsFtp *client, const char *path);
int wsftp_remove(WsFtp *client, const char *path, int directory);
int wsftp_rename(WsFtp *client, const char *from, const char *to, int replace);
int wsftp_atomic_replace(WsFtp *client);
int wsftp_download(WsFtp *client, const char *remote, uint64_t size,
                    WsFtpWrite output, WsFtpProgress progress, void *context);
/* Upload creates a NEW path exclusively. The caller stages and then renames it. */
int wsftp_upload(WsFtp *client, const char *remote, uint64_t size,
                  WsFtpRead input, WsFtpProgress progress, void *context);
/* Safe portable download component (not a path). Rejects Windows device names. */
int wsftp_local_name(const char *name);
#ifdef __cplusplus
}
#endif
