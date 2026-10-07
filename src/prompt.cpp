#include "core.hpp"
#include "ui.hpp"
#include "prompt.h"

namespace {
struct Prompt { HWND edit{}; std::wstring label, initial; bool secret{}, done{}, accepted{}; };
LRESULT CALLBACK promptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto p = reinterpret_cast<Prompt *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) { p = static_cast<Prompt *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p); }
    if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        p->edit = ui::edit(hwnd, L"", 10);
        if (p->secret) SendMessageW(p->edit, EM_SETPASSWORDCHAR, 0x2022, 0);
        SendMessageW(p->edit, EM_SETLIMITTEXT, 4096, 0);
        SetWindowTextW(p->edit, p->initial.c_str());
        ui::place(p->edit, 24, 145, 510, 30);
        ui::place(ui::button(hwnd, L"Cancel", IDCANCEL), 300, 200, 110, 36);
        ui::place(ui::button(hwnd, L"Continue", IDOK), 424, 200, 110, 36);
        ui::dark(hwnd); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) { p->accepted = LOWORD(wp) == IDOK; p->done = true; } return 0;
    case WM_CLOSE: p->done = true; return 0;
    case WM_DRAWITEM: ui::drawButton((DRAWITEMSTRUCT *)lp, wp == IDOK); return TRUE;
    case WM_CTLCOLOREDIT: SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::raised); return (LRESULT)GetStockObject(DC_BRUSH) + (SetDCBrushColor((HDC)wp, ui::raised), 0);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, p->label, ui::rect(24, 20, 510, 110), ui::TextSize::body, ui::text, false, DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &ps); return 0; }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
extern "C" int wsPrompt(HWND owner, const char *title, const char *label, int secret, char *value, int capacity) {
    if (capacity < 1) return 0;
    try {
        if (owner) { auto dpi = GetDpiForWindow(owner); if (dpi) ui::dpi = dpi; }
        Prompt p; p.label = wook::wide(label); p.initial = wook::wide(value); p.secret = secret != 0;
        WNDCLASSW wc{}; wc.lpfnWndProc = promptProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"wShell.TextPrompt"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
        RECT r{0,0,ui::px(558),ui::px(260)}; AdjustWindowRectEx(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        RECT parent{}; if (!GetWindowRect(owner, &parent)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &parent, 0);
        HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, wook::wide(title).c_str(), WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            parent.left + (parent.right-parent.left-r.right+r.left)/2, parent.top + (parent.bottom-parent.top-r.bottom+r.top)/2,
            r.right-r.left,r.bottom-r.top,owner,nullptr,wc.hInstance,&p);
        if (!dialog) return 0;
        DWORD pid{}; GetWindowThreadProcessId(owner, &pid);
        bool disable = pid == GetCurrentProcessId() && IsWindowEnabled(owner);
        if (disable) EnableWindow(owner, FALSE);
        ShowWindow(dialog, SW_SHOW); SetForegroundWindow(dialog); SetFocus(p.edit); SendMessageW(p.edit, EM_SETSEL, 0, -1);
        MSG msg{};
        while (!p.done) {
            int next = GetMessageW(&msg, nullptr, 0, 0);
            if (next <= 0) { if (!next) PostQuitMessage((int)msg.wParam); break; }
            if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
                SendMessageW(dialog, WM_COMMAND, msg.wParam == VK_RETURN ? IDOK : IDCANCEL, 0); continue;
            }
            if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (p.accepted) {
            auto wide = ui::value(p.edit); auto text = wook::utf8(wide);
            if (text.size() >= (size_t)capacity) p.accepted = false;
            else memcpy(value, text.c_str(), text.size()+1);
            SecureZeroMemory(wide.data(), wide.size()*sizeof(wchar_t)); SecureZeroMemory(text.data(), text.size());
        }
        SetWindowTextW(p.edit, L""); DestroyWindow(dialog);
        SecureZeroMemory(p.initial.data(), p.initial.size()*sizeof(wchar_t));
        if (disable) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
        return p.accepted;
    } catch (...) { return 0; }
}
