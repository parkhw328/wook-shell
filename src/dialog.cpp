#include "dialog.hpp"
#include "ui.hpp"
#include <commdlg.h>
#include <stdexcept>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#endif

namespace {
enum { Name = 300, Host, Protocol, Port, User, Key, Group, FontSize, Browse, Save, Cancel };
struct Form {
    wook::Profile profile;
    HWND fields[8]{};
    bool accepted = false, done = false, existing = false;
};
static const wchar_t *protocols[] = {L"ssh", L"telnet", L"rlogin", L"raw", L"serial"};
LRESULT CALLBACK dialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *form = (Form *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        form = (Form *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)form);
        ui::dark(hwnd);
        auto add = [&](int id, const wchar_t *cue, int x, int y, int w, const std::wstring &value) {
            auto h = ui::edit(hwnd, cue, id); form->fields[id - Name] = h;
            ui::place(h, x, y, w, 25); SetWindowTextW(h, value.c_str()); return h;
        };
        add(Name, L"e.g. Production API", 32, 91, 475, form->profile.name);
        add(Host, L"hostname or IP address", 32, 165, 475, form->profile.host);
        HWND combo = ui::control(hwnd, L"COMBOBOX", L"", Protocol, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
        form->fields[Protocol - Name] = combo; ui::place(combo, 28, 233, 220, 220);
        int current = 0;
        for (int i = 0; i < 5; ++i) { SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)protocols[i]); if (form->profile.protocol == protocols[i]) current = i; }
        SendMessageW(combo, CB_SETCURSEL, current, 0);
        add(Port, L"22", 280, 236, 226, std::to_wstring(form->profile.port));
        add(User, L"Prompt on connection", 32, 308, 220, form->profile.user);
        add(Group, L"e.g. Production", 280, 308, 226, form->profile.group);
        add(Key, L"Optional .ppk key file", 32, 381, 381, form->profile.keyFile);
        ui::place(ui::button(hwnd, L"Browse", Browse), 423, 374, 92, 38);
        add(FontSize, L"11", 32, 455, 110, std::to_wstring(form->profile.fontSize));
        ui::place(ui::button(hwnd, L"Cancel", Cancel), 288, 548, 100, 40);
        ui::place(ui::button(hwnd, L"Save host", Save), 398, 548, 118, 40);
        SetFocus(form->fields[0]);
#ifdef WOOK_UI_TEST
        SetTimer(hwnd, 90, 600, nullptr);
#endif
        return 0;
    }
    switch (msg) {
#ifdef WOOK_UI_TEST
    case WM_TIMER:
        KillTimer(hwnd, 90);
        captureTestWindow(hwnd, L"ui-host-editor.bmp");
        PostMessageW(hwnd, WM_COMMAND, Save, 0);
        return 0;
#endif
    case WM_PAINT: {
        PAINTSTRUCT ps; auto dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, form->existing ? L"Edit host" : L"A new connection", ui::rect(28, 17, 490, 34), 18, ui::bright, true);
        const struct { const wchar_t *text; int x, y; } labels[] = {
            {L"NAME",28,61}, {L"ADDRESS / SERIAL PORT",28,135}, {L"PROTOCOL",28,206}, {L"PORT / BAUD",276,206},
            {L"USERNAME",28,278}, {L"GROUP",276,278}, {L"PRIVATE KEY (.ppk)",28,351}, {L"FONT SIZE",28,425}
        };
        for (auto l : labels) ui::label(dc, l.text, ui::rect(l.x, l.y, 230, 20), 9, ui::muted, true);
        const RECT boxes[] = {ui::rect(27,83,489,41), ui::rect(27,157,489,41), ui::rect(275,228,241,41),
            ui::rect(27,300,239,41), ui::rect(275,300,241,41), ui::rect(27,373,390,41), ui::rect(27,447,124,41)};
        for (auto b : boxes) ui::round(dc, b, ui::raised);
        ui::label(dc, L"Passwords stay in the terminal. Your key is never copied.", ui::rect(28,503,490,23), 9, ui::muted);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp; SetTextColor(dc, ui::text); SetBkColor(dc, ui::raised);
        static HBRUSH b = CreateSolidBrush(ui::raised); return (LRESULT)b;
    }
    case WM_DRAWITEM: ui::drawButton((DRAWITEMSTRUCT *)lp, ((DRAWITEMSTRUCT *)lp)->CtlID == Save); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == Protocol && HIWORD(wp) == CBN_SELCHANGE) {
            int i = (int)SendMessageW(form->fields[Protocol - Name], CB_GETCURSEL, 0, 0);
            SetWindowTextW(form->fields[Port - Name], std::to_wstring(wook::defaultPort(protocols[i])).c_str());
        } else if (LOWORD(wp) == Browse) {
            wchar_t path[32768]{}; OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner = hwnd; ofn.lpstrFile = path;
            ofn.nMaxFile = 32768; ofn.lpstrFilter = L"PuTTY private keys (*.ppk)\0*.ppk\0All files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(form->fields[Key - Name], path);
        } else if (LOWORD(wp) == Save || LOWORD(wp) == IDOK) {
            try {
                auto p = form->profile;
                p.name = wook::trim(ui::value(form->fields[Name - Name])); p.host = wook::trim(ui::value(form->fields[Host - Name]));
                if (p.host.starts_with(L"[") && p.host.ends_with(L"]")) p.host = p.host.substr(1, p.host.size() - 2);
                p.user = wook::trim(ui::value(form->fields[User - Name])); p.group = wook::trim(ui::value(form->fields[Group - Name]));
                p.keyFile = wook::trim(ui::value(form->fields[Key - Name]));
                p.protocol = protocols[SendMessageW(form->fields[Protocol - Name], CB_GETCURSEL, 0, 0)];
                auto portText = ui::value(form->fields[Port - Name]), fontText = ui::value(form->fields[FontSize - Name]);
                if (portText.empty() || fontText.empty() || portText.find_first_not_of(L"0123456789") != std::wstring::npos || fontText.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Port and font size must be numbers.");
                p.port = std::stoi(portText); p.fontSize = std::stoi(fontText);
                wook::saveProfile(p, form->existing ? form->profile.name : L"");
                form->profile = p; form->accepted = true; DestroyWindow(hwnd);
            } catch (const std::exception &e) { ui::error(hwnd, e); }
        } else if (LOWORD(wp) == Cancel || LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: form->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
bool editHost(HWND owner, wook::Profile &profile, bool existing) {
    WNDCLASSW wc{}; wc.lpfnWndProc = dialogProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WookHostEditor"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    Form form; form.profile = profile; form.existing = existing;
    RECT r{0,0,ui::px(545),ui::px(612)}; AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, 0, ui::dpi);
    RECT parent; GetWindowRect(owner, &parent);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"wShell · Host", WS_CAPTION | WS_SYSMENU,
        parent.left + ((parent.right - parent.left) - (r.right - r.left)) / 2,
        parent.top + std::max(0L, ((parent.bottom - parent.top) - (r.bottom - r.top)) / 2), r.right - r.left, r.bottom - r.top,
        owner, nullptr, wc.hInstance, &form);
    if (!dialog) return false;
    EnableWindow(owner, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(form.fields[0]);
    MSG msg;
    while (!form.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    if (form.accepted) profile = form.profile;
    return form.accepted;
}
void showAbout(HWND owner) {
    MessageBoxW(owner,
        L"wShell 0.1.0\nA quiet workspace for your servers.\n\n"
        L"Native Windows x64 · Portable · MIT License\n\n"
        L"PuTTY 0.85 — modified portable build (MIT)\nFlexoki — Steph Ango (MIT)\nJetBrains Mono — SIL OFL 1.1\n\n"
        L"Full copyright and license notices are included in the licenses folder.\n"
        L"Independent project; not affiliated with PuTTY or Termius.\n\n"
        L"Ctrl+Shift+T  New connection\nCtrl+Shift+D  Duplicate tab\nCtrl+Tab  Next tab\nCtrl+Shift+W  Close tab\n"
        L"Ctrl+Shift+P  Find hosts\nCtrl+Shift+C / V  Copy / paste\nAlt+1…9  Switch tabs\nF11  Full screen",
        L"About wShell", MB_OK | MB_ICONINFORMATION);
}
