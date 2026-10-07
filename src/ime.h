#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WsIme WsIme;
typedef void (*WsImeSend)(void *context, const wchar_t *text, int length);
typedef int (*WsImeWidth)(unsigned int codepoint);

WsIme *wsImeCreate(HWND terminal, WsImeSend send, WsImeWidth width, void *context);
void wsImeDestroy(WsIme *ime);
void wsImePosition(WsIme *ime, int x, int y, int cellWidth, int cellHeight,
                   HFONT font, COLORREF foreground, COLORREF background);
void wsImeStart(WsIme *ime);
void wsImeClear(WsIme *ime);
void wsImeFinish(WsIme *ime);
void wsImeComposition(WsIme *ime, LPARAM flags, WPARAM character);
/* An immutable IMM snapshot. A negative preeditLength leaves the preview unchanged.
 * Only result is sent; preedit never becomes terminal or network input. */
void wsImeApply(WsIme *ime, const wchar_t *result, int resultLength,
                const wchar_t *preedit, int preeditLength, int cursor);

#ifdef __cplusplus
}
#endif
