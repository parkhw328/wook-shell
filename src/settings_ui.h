#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*WsSettingsSelect)(void *context, const char *path);
typedef void (*WsSettingsAccept)(void *context, int accept);
enum WsSettingsKind { WS_SETTINGS_CONNECTION, WS_SETTINGS_SESSION, WS_SETTINGS_AUTHORITIES };
HWND wsSettingsBegin(HWND window, enum WsSettingsKind kind, void *context, WsSettingsSelect select, WsSettingsAccept accept);
void wsSettingsAddPage(HWND window, const char *path, const char *description, const char *keywords);
void wsSettingsReady(HWND window);
void wsSettingsPageReady(HWND window, const char *path);
BOOL wsSettingsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, INT_PTR *result);
void wsSettingsEnd(HWND window);
void wsSettingsStyleControl(HWND control);
#ifdef __cplusplus
}
#endif
