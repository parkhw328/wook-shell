#pragma once
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WsKey WsKey;
WsKey *wsKeyGenerate(int rsaBits);
WsKey *wsKeyLoad(const wchar_t *path, const char *passphrase);
int wsKeySave(WsKey *key, const wchar_t *path, const char *passphrase);
char *wsKeyPublic(WsKey *key);
char *wsKeyFingerprint(WsKey *key);
const char *wsKeyError(void);
void wsKeyStringFree(char *text);
void wsKeyFree(WsKey *key);
#ifdef __cplusplus
}
#endif
