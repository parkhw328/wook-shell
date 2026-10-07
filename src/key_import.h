#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns a malloc-allocated PPK path, or NULL on cancellation/error. */
wchar_t *wsPreparePrivateKey(HWND owner, const wchar_t *path);
#ifdef __cplusplus
}
#endif
