#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WS_LIMIT (16 * 1024 * 1024)

static char *copyText(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static wchar_t *appendPath(const wchar_t *a, const wchar_t *b) {
    size_t n = wcslen(a) + wcslen(b) + 2;
    wchar_t *p = (wchar_t *)calloc(n, sizeof(wchar_t));
    if (p) swprintf(p, n, L"%ls\\%ls", a, b);
    return p;
}

wchar_t *wsRoot(void) {
    wchar_t *base = (wchar_t *)calloc(32768, sizeof(wchar_t));
    if (!base) return NULL;
    DWORD n = GetEnvironmentVariableW(L"WOOK_DATA_DIR", base, 32768);
    if (n >= 32768) { free(base); return NULL; }
    if (!n) {
        n = GetModuleFileNameW(NULL, base, 32768);
        if (!n || n >= 32768) { free(base); return NULL; }
        wchar_t *slash = wcsrchr(base, L'\\');
        if (!slash) { free(base); return NULL; }
        *slash = 0;
        wchar_t *next = appendPath(base, L"data");
        free(base);
        base = next;
    }
    if (base) CreateDirectoryW(base, NULL);
    return base;
}

static int nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static char *hexEncode(const char *text) {
    static const char digits[] = "0123456789abcdef";
    size_t len = strlen(text);
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        out[i * 2] = digits[c >> 4];
        out[i * 2 + 1] = digits[c & 15];
    }
    out[len * 2] = 0;
    return out;
}

static char *hexDecode(const char *text, size_t len) {
    if (len % 2) return NULL;
    char *out = (char *)malloc(len / 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i += 2) {
        int a = nibble(text[i]), b = nibble(text[i + 1]);
        if (a < 0 || b < 0 || (a == 0 && b == 0)) { free(out); return NULL; }
        out[i / 2] = (char)(a * 16 + b);
    }
    out[len / 2] = 0;
    return out;
}

wchar_t *wsPath(const wchar_t *category, const char *name) {
    if (!name || strlen(name) > 100 || wcspbrk(category, L"\\/.:")) return NULL;
    wchar_t *root = wsRoot();
    if (!root) return NULL;
    wchar_t *folder = appendPath(root, category);
    free(root);
    if (!folder) return NULL;
    CreateDirectoryW(folder, NULL);
    char *encoded = hexEncode(name);
    if (!encoded) { free(folder); return NULL; }
    wchar_t leaf[250];
    size_t n = strlen(encoded);
    for (size_t i = 0; i < n; ++i) leaf[i] = (wchar_t)encoded[i];
    wcscpy(leaf + n, L".ws");
    wchar_t *path = appendPath(folder, leaf);
    free(encoded);
    free(folder);
    return path;
}

const char *wsGet(const WsStore *store, const char *key) {
    if (store) for (WsPair *p = store->pairs; p; p = p->next)
        if (!strcmp(p->key, key)) return p->value;
    return NULL;
}

int wsSet(WsStore *store, const char *key, const char *value) {
    if (!store) return 0;
    if (!key || !value || !*key || strlen(key) > 65536 || strlen(value) > WS_LIMIT / 4) { store->failed = 1; return 0; }
    char *copy = copyText(value);
    if (!copy) { store->failed = 1; return 0; }
    for (WsPair *p = store->pairs; p; p = p->next) {
        if (!strcmp(p->key, key)) { free(p->value); p->value = copy; return 1; }
    }
    WsPair *pair = (WsPair *)calloc(1, sizeof(WsPair));
    if (!pair) { free(copy); store->failed = 1; return 0; }
    pair->key = copyText(key);
    pair->value = copy;
    if (!pair->key) { free(copy); free(pair); store->failed = 1; return 0; }
    pair->next = store->pairs;
    store->pairs = pair;
    return 1;
}

WsStore *wsOpen(const wchar_t *path, int writable) {
    if (!path) return NULL;
    WsStore *store = (WsStore *)calloc(1, sizeof(WsStore));
    if (!store) return NULL;
    store->lock = INVALID_HANDLE_VALUE;
    store->path = _wcsdup(path);
    if (!store->path) { wsClose(store); return NULL; }
    if (writable) {
        size_t len = wcslen(path) + 6;
        wchar_t *lockPath = (wchar_t *)calloc(len, sizeof(wchar_t));
        if (!lockPath) { wsClose(store); return NULL; }
        swprintf(lockPath, len, L"%ls.lock", path);
        for (int attempt = 0; attempt < 100; ++attempt) {
            store->lock = CreateFileW(lockPath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (store->lock != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
            Sleep(10);
        }
        free(lockPath);
        if (store->lock == INVALID_HANDLE_VALUE) { wsClose(store); return NULL; }
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return store;
        wsClose(store); return NULL;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 4 || size.QuadPart > WS_LIMIT) {
        CloseHandle(file); wsClose(store); return NULL;
    }
    DWORD count = (DWORD)size.QuadPart, read = 0;
    char *data = (char *)calloc((size_t)count + 1, 1);
    int valid = data && ReadFile(file, data, count, &read, NULL) && read == count;
    CloseHandle(file);
    if (!valid || memcmp(data, "WS1\n", 4) || memchr(data, 0, count)) { free(data); wsClose(store); return NULL; }
    char *cursor = data + 4;
    int entries = 0;
    while (*cursor) {
        char *end = strchr(cursor, '\n');
        char *equal = strchr(cursor, '=');
        if (!end || !equal || equal >= end || ++entries > 10000) { valid = 0; break; }
        char *key = hexDecode(cursor, (size_t)(equal - cursor));
        char *value = hexDecode(equal + 1, (size_t)(end - equal - 1));
        if (!key || !*key || !value || !wsSet(store, key, value)) valid = 0;
        free(key); free(value);
        if (!valid) break;
        cursor = end + 1;
    }
    free(data);
    if (!valid) { wsClose(store); return NULL; }
    store->exists = 1;
    return store;
}

static int writeBytes(HANDLE file, const char *data, size_t len) {
    DWORD written;
    return len <= WS_LIMIT && WriteFile(file, data, (DWORD)len, &written, NULL) && written == len;
}

int wsSave(WsStore *store) {
    if (!store || store->lock == INVALID_HANDLE_VALUE || store->failed) return 0;
    size_t size = wcslen(store->path) + 80;
    wchar_t *tmp = (wchar_t *)calloc(size, sizeof(wchar_t));
    if (!tmp) return 0;
    swprintf(tmp, size, L"%ls.%lu.%llu.tmp", store->path, GetCurrentProcessId(), GetTickCount64());
    HANDLE file = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(tmp); return 0; }
    int ok = writeBytes(file, "WS1\n", 4);
    size_t total = 4;
    for (WsPair *p = store->pairs; ok && p; p = p->next) {
        char *key = hexEncode(p->key), *value = hexEncode(p->value);
        if (!key || !value) { free(key); free(value); ok = 0; break; }
        total += strlen(key) + strlen(value) + 2;
        ok = total <= WS_LIMIT && writeBytes(file, key, strlen(key)) && writeBytes(file, "=", 1)
            && writeBytes(file, value, strlen(value)) && writeBytes(file, "\n", 1);
        free(key); free(value);
    }
    if (ok) ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(tmp, store->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp);
    free(tmp);
    return ok;
}

void wsClose(WsStore *store) {
    if (!store) return;
    if (store->lock != INVALID_HANDLE_VALUE) CloseHandle(store->lock);
    WsPair *pair = store->pairs;
    while (pair) { WsPair *next = pair->next; free(pair->key); free(pair->value); free(pair); pair = next; }
    free(store->path);
    free(store);
}

int wsRemove(const wchar_t *path) {
    WsStore *store = wsOpen(path, 1);
    if (!store) return 0;
    int ok = DeleteFileW(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
    wsClose(store);
    return ok;
}

char **wsList(const wchar_t *category, size_t *count) {
    *count = 0;
    wchar_t *root = wsRoot();
    if (!root) return NULL;
    wchar_t *folder = appendPath(root, category);
    free(root);
    if (!folder) return NULL;
    wchar_t *pattern = appendPath(folder, L"*.ws");
    free(folder);
    if (!pattern) return NULL;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) return NULL;
    char **items = NULL;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t len = wcslen(fd.cFileName);
        if (len < 3 || len > 243) continue;
        char name[250];
        for (size_t i = 0; i < len - 3; ++i) name[i] = (char)fd.cFileName[i];
        char *decoded = hexDecode(name, len - 3);
        if (!decoded) continue;
        char **next = (char **)realloc(items, (*count + 1) * sizeof(char *));
        if (!next) { free(decoded); break; }
        items = next;
        items[(*count)++] = decoded;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return items;
}

void wsFreeList(char **items, size_t count) {
    for (size_t i = 0; i < count; ++i) free(items[i]);
    free(items);
}
