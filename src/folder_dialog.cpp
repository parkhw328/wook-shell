#include "dialog.hpp"
#include "connections.hpp"
#include "ui.hpp"
#include <stdexcept>

namespace {
struct FolderForm {
    std::wstring name;
    bool existing = false, accepted = false, done = false;
};
LRESULT CALLBACK folderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto form = (FolderForm *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        form = (FolderForm *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)form);
    }
    if (!form) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        ui::dark(hwnd);
        auto name = ui::edit(hwnd, L"e.g. Production", 1001);
        ui::place(name, 30, 88, 420, 28); SetWindowTextW(name, form->name.c_str());
        SendMessageW(name, EM_SETLIMITTEXT, 100, 0);
        ui::place(ui::button(hwnd, L"Cancel", IDCANCEL), 208, 156, 112, 38);
        ui::place(ui::button(hwnd, L"Save folder", IDOK), 332, 156, 120, 38);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) form->done = true;
        else if (LOWORD(wp) == IDOK) {
            try {
                auto name = wook::trim(ui::value(GetDlgItem(hwnd, 1001)));
                if (form->existing) wook::renameFolder(form->name, name); else wook::createFolder(name);
                form->name = name; form->accepted = true; form->done = true;
            } catch (const std::exception &error) { ui::error(hwnd, error); }
        }
        return 0;
    case WM_CLOSE: form->done = true; return 0;
    case WM_DRAWITEM: ui::drawButton((DRAWITEMSTRUCT *)lp, wp == IDOK); return TRUE;
    case WM_CTLCOLOREDIT: {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::raised);
        static HBRUSH brush = CreateSolidBrush(ui::raised); return (LRESULT)brush;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); RECT rect{}; GetClientRect(hwnd, &rect); ui::fill(dc, rect, ui::panel);
        ui::label(dc, form->existing ? L"Rename folder" : L"New folder", ui::rect(28, 18, 424, 34), ui::TextSize::title, ui::bright, true);
        ui::label(dc, L"FOLDER NAME", ui::rect(28, 59, 424, 22), ui::TextSize::caption, ui::muted);
        EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
bool editFolder(HWND owner, std::wstring &name, bool existing) {
    FolderForm form{name, existing};
    WNDCLASSW wc{}; wc.lpfnWndProc = folderProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShell.FolderEditor"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    RECT size{0, 0, ui::px(480), ui::px(216)}, parent{};
    AdjustWindowRectExForDpi(&size, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, ui::dpi);
    GetWindowRect(owner, &parent); auto previous = GetFocus();
    auto dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"wShell · Folder", WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        parent.left + (parent.right - parent.left - size.right + size.left) / 2,
        parent.top + (parent.bottom - parent.top - size.bottom + size.top) / 2,
        size.right - size.left, size.bottom - size.top, owner, nullptr, wc.hInstance, &form);
    if (!dialog) throw std::runtime_error("Cannot open the folder editor.");
    bool enabled = IsWindowEnabled(owner) != FALSE;
    if (enabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOWNORMAL); SetForegroundWindow(dialog); SetFocus(GetDlgItem(dialog, 1001));
    SendDlgItemMessageW(dialog, 1001, EM_SETSEL, 0, -1);
    MSG msg{};
    while (!form.done) {
        int next = GetMessageW(&msg, nullptr, 0, 0);
        if (next <= 0) { if (!next) PostQuitMessage((int)msg.wParam); break; }
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN)) {
            SendMessageW(dialog, WM_COMMAND, msg.wParam == VK_ESCAPE || GetFocus() == GetDlgItem(dialog, IDCANCEL) ? IDCANCEL : IDOK, 0);
            continue;
        }
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DestroyWindow(dialog);
    if (enabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE); SetActiveWindow(owner);
        if (IsWindow(previous) && (previous == owner || IsChild(owner, previous))) SetFocus(previous);
    }
    if (form.accepted) name = form.name;
    return form.accepted;
}
