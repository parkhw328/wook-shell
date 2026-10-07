#include "ime.h"
#include <imm.h>
#include <stdlib.h>
#include <string.h>

enum { WS_IME_LIMIT = 1024 };
struct WsIme {
    HWND terminal, overlay;
    WsImeSend send;
    WsImeWidth width;
    void *context;
    wchar_t text[WS_IME_LIMIT];
    int length, cursor, x, y, cellWidth, cellHeight;
    BOOL active;
    HFONT font;
    COLORREF foreground, background;
};

static int scalar(const wchar_t *text, int length, int *index) {
    unsigned int cp = text[(*index)++];
    if (cp >= 0xd800 && cp <= 0xdbff && *index < length &&
        text[*index] >= 0xdc00 && text[*index] <= 0xdfff)
        cp = 0x10000 + ((cp - 0xd800) << 10) + text[(*index)++] - 0xdc00;
    return cp;
}

static int textPixels(WsIme *ime, int length) {
    int cells = 0;
    for (int i = 0; i < length;) {
        int width = ime->width(scalar(ime->text, length, &i));
        cells += width < 0 ? 1 : width;
    }
    return cells * ime->cellWidth;
}

static LRESULT CALLBACK overlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    WsIme *ime = (WsIme *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ime = ((CREATESTRUCTW *)lParam)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ime);
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT && ime) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT bounds; GetClientRect(window, &bounds);
        HBRUSH background = CreateSolidBrush(ime->background);
        FillRect(dc, &bounds, background); DeleteObject(background);
        HFONT previous = SelectObject(dc, ime->font);
        SetTextColor(dc, ime->foreground); SetBkMode(dc, TRANSPARENT);
        int advances[WS_IME_LIMIT] = {0};
        for (int i = 0; i < ime->length;) {
            int width = ime->width(scalar(ime->text, ime->length, &i));
            advances[i - 1] = (width < 0 ? 1 : width) * ime->cellWidth;
        }
        /* Match the terminal grid, including two-cell Hangul and surrogate pairs.
         * GDI font linking supplies glyphs absent from the bundled Latin font. */
        ExtTextOutW(dc, 0, 0, ETO_CLIPPED, &bounds, ime->text, ime->length, advances);
        HBRUSH accent = CreateSolidBrush(RGB(0xda, 0x70, 0x2c));
        RECT underline = {0, bounds.bottom - 2, bounds.right, bounds.bottom};
        FillRect(dc, &underline, accent);
        int x = textPixels(ime, ime->cursor);
        if (x >= bounds.right) x = bounds.right - 1;
        RECT caret = {x, 1, x + 1, bounds.bottom - 2};
        FillRect(dc, &caret, accent); DeleteObject(accent);
        SelectObject(dc, previous); EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

WsIme *wsImeCreate(HWND terminal, WsImeSend send, WsImeWidth width, void *context) {
    WsIme *ime = calloc(1, sizeof(*ime));
    if (!ime) return NULL;
    ime->terminal = terminal; ime->send = send; ime->width = width; ime->context = context;
    ime->cellWidth = 8; ime->cellHeight = 16;
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = overlayProc; cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"WShellImeComposition";
    RegisterClassW(&cls);
    ime->overlay = CreateWindowExW(WS_EX_NOACTIVATE, cls.lpszClassName, L"",
        WS_CHILD | WS_DISABLED, 0, 0, 1, 1, terminal, NULL, cls.hInstance, ime);
    if (!ime->overlay) { free(ime); return NULL; }
    /* The preview is display-only and must not create another IME context. */
    ImmAssociateContext(ime->overlay, NULL);
    return ime;
}

void wsImeClear(WsIme *ime) {
    if (!ime) return;
    ime->active = FALSE; ime->length = ime->cursor = 0;
    SecureZeroMemory(ime->text, sizeof(ime->text));
    RECT old; GetWindowRect(ime->overlay, &old);
    MapWindowPoints(NULL, ime->terminal, (POINT *)&old, 2);
    ShowWindow(ime->overlay, SW_HIDE);
    InvalidateRect(ime->terminal, &old, FALSE);
}

void wsImeDestroy(WsIme *ime) {
    if (!ime) return;
    if (IsWindow(ime->overlay)) DestroyWindow(ime->overlay);
    SecureZeroMemory(ime, sizeof(*ime)); free(ime);
}

static void position(WsIme *ime) {
    if (!ime->active) return;
    RECT client; GetClientRect(ime->terminal, &client);
    int width = textPixels(ime, ime->length);
    if (width < ime->cellWidth) width = ime->cellWidth;
    if (width > client.right) width = client.right;
    int x = ime->x, y = ime->y;
    if (x + width > client.right) x = client.right - width;
    if (y + ime->cellHeight > client.bottom) y = client.bottom - ime->cellHeight;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(ime->overlay, HWND_TOP, x, y, width, ime->cellHeight, SWP_NOACTIVATE);
    if (ime->length && ime->active) {
        ShowWindow(ime->overlay, SW_SHOWNOACTIVATE);
        InvalidateRect(ime->overlay, NULL, FALSE);
    }
    /* Keep the OS candidate UI; exclude our composition row from its placement. */
    HIMC context = ImmGetContext(ime->terminal);
    if (context) {
        int caret = x + textPixels(ime, ime->cursor);
        if (caret >= client.right) caret = client.right > 0 ? client.right - 1 : 0;
        CANDIDATEFORM form = {0};
        form.dwStyle = CFS_EXCLUDE; form.ptCurrentPos.x = caret; form.ptCurrentPos.y = y;
        SetRect(&form.rcArea, x, y, x + width, y + ime->cellHeight);
        for (DWORD i = 0; i < 4; ++i) { form.dwIndex = i; ImmSetCandidateWindow(context, &form); }
        ImmReleaseContext(ime->terminal, context);
    }
}

void wsImePosition(WsIme *ime, int x, int y, int cellWidth, int cellHeight,
                   HFONT font, COLORREF foreground, COLORREF background) {
    if (!ime || x < 0 || y < 0 || cellWidth < 1 || cellHeight < 1) return;
    ime->x = x; ime->y = y; ime->cellWidth = cellWidth; ime->cellHeight = cellHeight;
    ime->font = font; ime->foreground = foreground; ime->background = background;
    position(ime);
}

void wsImeStart(WsIme *ime) {
    if (!ime) return;
    wsImeClear(ime); ime->active = TRUE;
    position(ime);
}

void wsImeFinish(WsIme *ime) {
    if (!ime || !ime->active) return;
    HIMC context = ImmGetContext(ime->terminal);
    if (context) {
        /* Complete into the OLD tab before focus changes. The resulting message
         * owns the commit; never send the preview ourselves. */
        ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        ImmReleaseContext(ime->terminal, context);
    }
    wsImeClear(ime);
}

void wsImeApply(WsIme *ime, const wchar_t *result, int resultLength,
                const wchar_t *preedit, int preeditLength, int cursor) {
    if (!ime) return;
    if (result && resultLength > 0) {
        wsImeClear(ime);
        /* Preserve PuTTY's per-codepoint Korean input behavior, without splitting
         * supplementary characters or treating preview updates as keystrokes. */
        for (int i = 0; i < resultLength;) {
            int start = i; scalar(result, resultLength, &i);
            ime->send(ime->context, result + start, i - start);
        }
    }
    if (preeditLength >= 0) {
        SecureZeroMemory(ime->text, sizeof(ime->text));
        ime->length = preedit ? (preeditLength < WS_IME_LIMIT ? preeditLength : WS_IME_LIMIT - 1) : 0;
        if (ime->length && preedit[ime->length - 1] >= 0xd800 && preedit[ime->length - 1] <= 0xdbff) --ime->length;
        if (ime->length) memcpy(ime->text, preedit, ime->length * sizeof(wchar_t));
        ime->active = TRUE;
    }
    if (cursor >= 0) ime->cursor = cursor > ime->length ? ime->length : cursor;
    if (ime->cursor > ime->length) ime->cursor = ime->length;
    if (ime->cursor && ime->text[ime->cursor - 1] >= 0xd800 && ime->text[ime->cursor - 1] <= 0xdbff) --ime->cursor;
    if (!ime->length) ShowWindow(ime->overlay, SW_HIDE);
    position(ime);
}

static wchar_t *readString(HIMC context, DWORD flag, int *length) {
    *length = 0;
    LONG bytes = ImmGetCompositionStringW(context, flag, NULL, 0);
    if (bytes <= 0 || bytes > 1024 * 1024 || bytes % sizeof(wchar_t)) return NULL;
    wchar_t *text = malloc(bytes);
    if (!text) return NULL;
    LONG copied = ImmGetCompositionStringW(context, flag, text, bytes);
    if (copied != bytes) { SecureZeroMemory(text, bytes); free(text); return NULL; }
    *length = bytes / sizeof(wchar_t);
    return text;
}

void wsImeComposition(WsIme *ime, LPARAM flags, WPARAM character) {
    if (!ime) return;
    HIMC context = ImmGetContext(ime->terminal);
    if (!context) { wsImeClear(ime); return; }
    int resultLength = 0, preeditLength = -1;
    wchar_t *result = NULL, *preedit = NULL;
    if (flags & GCS_RESULTSTR) result = readString(context, GCS_RESULTSTR, &resultLength);
    if (flags & GCS_COMPSTR) preedit = readString(context, GCS_COMPSTR, &preeditLength);
    int cursor = (flags & GCS_CURSORPOS) ? ImmGetCompositionStringW(context, GCS_CURSORPOS, NULL, 0) : -1;
    ImmReleaseContext(ime->terminal, context);
    if ((flags & GCS_RESULTSTR) && !resultLength) wsImeClear(ime);
    if (!flags) wsImeClear(ime);
    else if ((flags & CS_INSERTCHAR) && !(flags & (GCS_COMPSTR | GCS_RESULTSTR))) {
        wchar_t preview = (wchar_t)character;
        wsImeApply(ime, NULL, 0, &preview, 1, (flags & CS_NOMOVECARET) ? 0 : 1);
    } else wsImeApply(ime, result, resultLength, preedit, preeditLength, cursor);
    if (result) { SecureZeroMemory(result, resultLength * sizeof(wchar_t)); free(result); }
    if (preedit) { SecureZeroMemory(preedit, preeditLength * sizeof(wchar_t)); free(preedit); }
}
