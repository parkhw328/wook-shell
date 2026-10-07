#include "core.hpp"
#include "ui.hpp"
#include "workspace_dialog.hpp"
#include <stdexcept>

namespace {
constexpr int preferenceCheck = 1001;
struct Dialog {
    bool settings{}, checked{}, done{}, accepted{};
    std::wstring title, message;
};
LRESULT CALLBACK dialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto form = (Dialog *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        form = (Dialog *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)form);
    }
    if (!form) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        ui::dark(hwnd);
        ui::place(ui::checkbox(hwnd, form->settings ? L"Confirm before closing tabs" : L"Don't ask again when closing tabs", preferenceCheck), 28, 137, 564, 28);
        SendDlgItemMessageW(hwnd, preferenceCheck, BM_SETCHECK, form->checked ? BST_CHECKED : BST_UNCHECKED, 0);
        ui::place(ui::button(hwnd, L"Cancel", IDCANCEL), 352, 222, 112, 36);
        ui::place(ui::button(hwnd, form->settings ? L"Save" : L"Close", IDOK), 480, 222, 112, 36);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { form->done = true; return 0; }
        if (LOWORD(wp) == IDOK) {
            try {
                bool checked = SendDlgItemMessageW(hwnd, preferenceCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (form->settings || checked) wook::saveConfirmCloseTabs(form->settings ? checked : false);
                form->accepted = true; form->done = true;
            } catch (const std::exception &e) { ui::error(hwnd, e); }
        }
        return 0;
    case WM_CLOSE: form->done = true; return 0;
    case WM_DRAWITEM: ui::drawButton((DRAWITEMSTRUCT *)lp, wp == IDOK); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, form->title, ui::rect(28, 20, 564, 35), ui::TextSize::title, ui::bright, true);
        ui::label(dc, form->message, ui::rect(28, 68, 564, 58), ui::TextSize::body, ui::text, false, DT_LEFT | DT_WORDBREAK);
        ui::label(dc, form->settings ? L"Turn this on to restore the close confirmation. Saved on this PC." :
                  L"Restore the confirmation in Tools → Application settings.",
                  ui::rect(28, 172, 564, 36), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
bool showDialog(HWND owner, Dialog &form, const wchar_t *caption) {
    WNDCLASSW wc{}; wc.lpfnWndProc = dialogProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShell.WorkspaceDialog"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    RECT size{0, 0, ui::px(620), ui::px(282)}, parent{};
    AdjustWindowRectExForDpi(&size, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, ui::dpi);
    GetWindowRect(owner, &parent);
    auto previous = GetFocus();
    auto dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, caption, WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        parent.left + (parent.right - parent.left - size.right + size.left) / 2,
        parent.top + (parent.bottom - parent.top - size.bottom + size.top) / 2,
        size.right - size.left, size.bottom - size.top, owner, nullptr, wc.hInstance, &form);
    if (!dialog) throw std::runtime_error("Cannot open application settings.");
    bool enabled = IsWindowEnabled(owner) != FALSE;
    if (enabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOWNORMAL); SetForegroundWindow(dialog); SetFocus(GetDlgItem(dialog, IDOK));
    MSG msg{};
    while (!form.done) {
        int next = GetMessageW(&msg, nullptr, 0, 0);
        if (next <= 0) { if (!next) PostQuitMessage((int)msg.wParam); break; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { form.done = true; continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            // Enter activates the focused button; elsewhere it uses Close/Save.
            auto focus = GetFocus();
            SendMessageW(dialog, WM_COMMAND, focus == GetDlgItem(dialog, IDCANCEL) ? IDCANCEL : IDOK, 0); continue;
        }
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DestroyWindow(dialog);
    if (enabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE); SetActiveWindow(owner);
        if (IsWindow(previous) && (previous == owner || IsChild(owner, previous))) SetFocus(previous);
    }
    return form.accepted;
}
}
bool confirmTabClose(HWND owner, size_t tabs, size_t live) {
    if (!live || !wook::loadConfirmCloseTabs()) return true;
    Dialog form;
    form.title = tabs == 1 ? L"Close this tab?" : L"Close " + std::to_wstring(tabs) + L" tabs?";
    form.message = std::to_wstring(live) + (live == 1 ? L" active session will disconnect." : L" active sessions will disconnect.") +
        L"\nCommands or file transfers may still be running.";
    return showDialog(owner, form, tabs == 1 ? L"Close tab" : L"Close tabs");
}
void showApplicationSettings(HWND owner) {
    Dialog form; form.settings = true; form.checked = wook::loadConfirmCloseTabs();
    form.title = L"Application settings";
    form.message = L"Choose how wShell confirms closing active terminal and SFTP tabs.";
    showDialog(owner, form, L"wShell · Application settings");
}
