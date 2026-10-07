#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 1 with an allocated UTF-8 password, 0 if absent/unmatched, -1 on failure. */
int wsLoadSavedPassword(const char *session, const char *host, int port, const char *user, char **password);
void wsPasswordFree(char *password);
#ifdef __cplusplus
}
#endif
