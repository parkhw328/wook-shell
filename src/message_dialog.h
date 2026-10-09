#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
int wsMessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags);
int wsMessageBoxA(HWND owner, LPCSTR text, LPCSTR title, UINT flags);
int wsMessageBoxIndirectW(const MSGBOXPARAMSW *params);
int wsHostKeyDialog(HWND owner, const char *title, const char *text, const char *details, int changed);
void wsShowAbout(HWND owner);
void wsInitializeMessageDialogs(void);
#ifdef __cplusplus
}
#endif
