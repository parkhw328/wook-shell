#pragma once
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WsKey WsKey;
typedef struct WsPublicKey WsPublicKey;
enum WsKeyFileKind { WS_KEY_INVALID = 0, WS_KEY_PPK, WS_KEY_IMPORT, WS_KEY_PUBLIC };
int wsKeyInspect(const wchar_t *path, int *encrypted);
WsKey *wsKeyGenerate(int rsaBits);
WsKey *wsKeyLoad(const wchar_t *path, const char *passphrase);
/* Public identity only; accepts OpenSSH/RFC4716 public files and PPKs. */
WsPublicKey *wsPublicKeyLoad(const wchar_t *path);
int wsPublicKeyMatchesFile(const WsPublicKey *publicKey, const wchar_t *path);
int wsPublicKeyMatchesKey(const WsPublicKey *publicKey, WsKey *key);
void wsPublicKeyFree(WsPublicKey *key);
int wsKeySave(WsKey *key, const wchar_t *path, const char *passphrase);
char *wsKeyPublic(WsKey *key);
char *wsKeyFingerprint(WsKey *key);
const char *wsKeyError(void);
void wsKeyStringFree(char *text);
void wsKeyFree(WsKey *key);
#ifdef __cplusplus
}
#endif
