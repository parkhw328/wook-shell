#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <map>
#include <string>
#include "message_dialog.h"

namespace ui {
inline UINT dpi = 96;
inline int px(int value) { return MulDiv(value, (int)dpi, 96); }
inline constexpr COLORREF bg = RGB(16, 15, 15), panel = RGB(28, 27, 26), raised = RGB(40, 39, 38);
inline constexpr COLORREF line = RGB(52, 51, 49), text = RGB(206, 205, 195), muted = RGB(135, 133, 128);
inline constexpr COLORREF accent = RGB(218, 112, 44), bright = RGB(255, 252, 240), red = RGB(209, 77, 65);
inline COLORREF tint(COLORREF color, COLORREF background, int percent) {
    return RGB((GetRValue(color)*percent+GetRValue(background)*(100-percent))/100,
               (GetGValue(color)*percent+GetGValue(background)*(100-percent))/100,
               (GetBValue(color)*percent+GetBValue(background)*(100-percent))/100);
}
enum class TextSize { caption = 9, body = 11, section = 13, title = 20 };
inline std::map<std::tuple<TextSize, bool, UINT>, HFONT> fonts;
// One typeface, two bundled weights, and a shared semantic scale in points.
inline HFONT font(TextSize size = TextSize::body, bool bold = false) {
    auto key = std::make_tuple(size, bold, dpi);
    if (auto it = fonts.find(key); it != fonts.end()) return it->second;
    auto f = CreateFontW(-MulDiv((int)size, dpi, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"JetBrains Mono");
    fonts[key] = f; return f;
}
inline void fill(HDC dc, RECT r, COLORREF color) { HBRUSH b = CreateSolidBrush(color); FillRect(dc, &r, b); DeleteObject(b); }
inline void round(HDC dc, RECT r, COLORREF color, COLORREF border = line, int radius = 9) {
    HBRUSH b = CreateSolidBrush(color); HPEN p = CreatePen(PS_SOLID, 1, border);
    auto oldB = SelectObject(dc, b); auto oldP = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, px(radius), px(radius));
    SelectObject(dc, oldB); SelectObject(dc, oldP); DeleteObject(b); DeleteObject(p);
}
inline void label(HDC dc, std::wstring value, RECT r, TextSize size = TextSize::body, COLORREF color = text,
                  bool bold = false, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    auto old = SelectObject(dc, font(size, bold));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), (int)value.size(), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
inline RECT rect(int x, int y, int w, int h) { return {px(x), px(y), px(x + w), px(y + h)}; }
inline void place(HWND hwnd, int x, int y, int w, int h) { MoveWindow(hwnd, px(x), px(y), px(w), px(h), TRUE); }
inline void dark(HWND hwnd) {
    BOOL on = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on));
    COLORREF caption = bg;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}
inline HWND control(HWND parent, const wchar_t *kind, const wchar_t *caption, int id, DWORD style) {
    HWND hwnd = CreateWindowExW(0, kind, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                              (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font(), TRUE);
    SetWindowTheme(hwnd, L"DarkMode_CFD", nullptr);
    return hwnd;
}
inline HWND button(HWND parent, const wchar_t *caption, int id) {
    return control(parent, L"BUTTON", caption, id, BS_OWNERDRAW | WS_TABSTOP);
}
inline LRESULT CALLBACK checkboxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_NCDESTROY) { RemoveWindowSubclass(hwnd, checkboxProc, id); return DefSubclassProc(hwnd, msg, wp, lp); }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r); fill(dc, r, panel);
        bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
        RECT box{px(1), (r.bottom - px(16)) / 2, px(17), (r.bottom + px(16)) / 2};
        round(dc, box, checked ? accent : raised, GetFocus() == hwnd ? accent : muted, 5);
        if (checked) label(dc, L"✓", box, TextSize::caption, bg, true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        int len = GetWindowTextLengthW(hwnd); std::wstring caption(len + 1, L'\0');
        GetWindowTextW(hwnd, caption.data(), len + 1); caption.resize(len); r.left += px(25);
        label(dc, caption, r, TextSize::body, IsWindowEnabled(hwnd) ? text : muted);
        EndPaint(hwnd, &ps); return 0;
    }
    auto result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == BM_SETCHECK || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE ||
        msg == WM_LBUTTONUP || msg == WM_KEYUP) InvalidateRect(hwnd, nullptr, TRUE);
    return result;
}
inline HWND checkbox(HWND parent, const wchar_t *caption, int id) {
    auto hwnd = control(parent, L"BUTTON", caption, id, BS_AUTOCHECKBOX | WS_TABSTOP);
    SetWindowSubclass(hwnd, checkboxProc, 1, 0); return hwnd;
}
inline HWND edit(HWND parent, const wchar_t *cue, int id) {
    auto hwnd = control(parent, L"EDIT", L"", id, ES_AUTOHSCROLL | WS_TABSTOP);
    SendMessageW(hwnd, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
    SendMessageW(hwnd, EM_SETLIMITTEXT, 512, 0);
    return hwnd;
}
inline std::wstring value(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring out((size_t)len + 1, L'\0'); GetWindowTextW(hwnd, out.data(), len + 1); out.resize(len); return out;
}
inline void drawButton(const DRAWITEMSTRUCT *d, bool primary = false, TextSize size = TextSize::body, const wchar_t *caption = nullptr) {
    bool disabled = (d->itemState & ODS_DISABLED) != 0, pressed = (d->itemState & ODS_SELECTED) != 0;
    COLORREF color = primary ? accent : (pressed ? line : raised);
    fill(d->hDC, d->rcItem, panel);
    round(d->hDC, d->rcItem, color, primary ? accent : line);
    label(d->hDC, caption ? caption : value(d->hwndItem), d->rcItem, size, disabled ? muted : primary ? bg : text, primary,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (d->itemState & ODS_FOCUS) {
        RECT focus = d->rcItem; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(d->hDC, &focus);
    }
}
inline void error(HWND owner, const std::exception &e) {
    auto str = wook::wide(e.what()); wsMessageBoxW(owner, str.c_str(), L"wShell", MB_OK | MB_ICONEXCLAMATION);
}
}
