#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <map>
#include <string>

namespace ui {
inline UINT dpi = 96;
inline int px(int value) { return MulDiv(value, (int)dpi, 96); }
inline constexpr COLORREF bg = RGB(16, 15, 15), panel = RGB(28, 27, 26), raised = RGB(40, 39, 38);
inline constexpr COLORREF line = RGB(52, 51, 49), text = RGB(206, 205, 195), muted = RGB(135, 133, 128);
inline constexpr COLORREF accent = RGB(58, 169, 159), bright = RGB(255, 252, 240), red = RGB(209, 77, 65);
inline std::map<std::tuple<int, bool, bool, UINT>, HFONT> fonts;
inline HFONT font(int size = 11, bool bold = false, bool mono = false) {
    auto key = std::make_tuple(size, bold, mono, dpi);
    if (auto it = fonts.find(key); it != fonts.end()) return it->second;
    auto f = CreateFontW(-MulDiv(size, dpi, 72), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, mono ? L"JetBrains Mono" : L"Segoe UI");
    fonts[key] = f; return f;
}
inline void fill(HDC dc, RECT r, COLORREF color) { HBRUSH b = CreateSolidBrush(color); FillRect(dc, &r, b); DeleteObject(b); }
inline void round(HDC dc, RECT r, COLORREF color, COLORREF border = line, int radius = 9) {
    HBRUSH b = CreateSolidBrush(color); HPEN p = CreatePen(PS_SOLID, 1, border);
    auto oldB = SelectObject(dc, b); auto oldP = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, px(radius), px(radius));
    SelectObject(dc, oldB); SelectObject(dc, oldP); DeleteObject(b); DeleteObject(p);
}
inline void label(HDC dc, std::wstring value, RECT r, int size = 11, COLORREF color = text,
                  bool bold = false, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, bool mono = false) {
    auto old = SelectObject(dc, font(size, bold, mono));
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
inline void drawButton(const DRAWITEMSTRUCT *d, bool primary = false) {
    bool disabled = (d->itemState & ODS_DISABLED) != 0, pressed = (d->itemState & ODS_SELECTED) != 0;
    COLORREF color = primary ? accent : (pressed ? line : raised);
    round(d->hDC, d->rcItem, color, primary ? accent : line);
    label(d->hDC, value(d->hwndItem), d->rcItem, 10, disabled ? muted : primary ? bg : text, primary,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (d->itemState & ODS_FOCUS) {
        RECT focus = d->rcItem; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(d->hDC, &focus);
    }
}
inline void error(HWND owner, const std::exception &e) {
    auto str = wook::wide(e.what()); MessageBoxW(owner, str.c_str(), L"Wook Shell", MB_OK | MB_ICONEXCLAMATION);
}
}
