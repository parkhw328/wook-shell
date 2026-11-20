#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct WsPair { char *key; char *value; struct WsPair *next; } WsPair;
typedef struct WsStore {
    wchar_t *path;
    HANDLE lock;
    WsPair *pairs;
    int exists;
    int failed;
} WsStore;

typedef struct WsApplicationSettings {
    bool confirmCloseTabs;
    bool terminalRightClickMenu;
} WsApplicationSettings;

/* UTF-8 names, Unicode paths, transactional writers, atomic replacement. */
wchar_t *wsRoot(void);
wchar_t *wsPath(const wchar_t *category, const char *name);
WsStore *wsOpen(const wchar_t *path, int writable);
WsStore *wsOpenWorkspaceLayout(int writable);
bool wsLoadApplicationSettings(WsApplicationSettings *settings);
const char *wsGet(const WsStore *store, const char *key);
int wsSet(WsStore *store, const char *key, const char *value);
int wsSave(WsStore *store);
void wsClose(WsStore *store);
int wsRemove(const wchar_t *path);
char **wsList(const wchar_t *category, size_t *count);
void wsFreeList(char **items, size_t count);
#ifdef __cplusplus
}
#endif
