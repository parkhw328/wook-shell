#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
int wsPrompt(HWND owner, const char *title, const char *label, int secret, char *value, int capacity);
#ifdef __cplusplus
}
#endif
