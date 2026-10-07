#include "dialog.hpp"
#include "../build/version.h"
#include "ui.hpp"
#include "key_dialog.hpp"
#include <commdlg.h>
#include <stdexcept>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#endif

namespace {
enum { Name = 300, Host, Protocol, Port, User, Key, Group, FontSize, Browse, Save, Cancel, Auth, Password, Remember, KeyManager, Alias, TabColor };
struct Form {
    wook::Profile profile;
    HWND fields[8]{};
    HWND auth{}, password{}, remember{}, alias{}, color{};
    bool accepted = false, done = false, existing = false;
};
static const wchar_t *protocols[] = {L"ssh", L"telnet", L"rlogin", L"raw", L"serial"};
static const wchar_t *colors[] = {L"", L"da702c", L"d14d41", L"d0a215", L"879a39", L"4385be", L"8b7ec8", L"ce5d97", L"878580"};
bool passwordMode(Form *form) {
    return SendMessageW(form->fields[Protocol - Name], CB_GETCURSEL, 0, 0) == 0 &&
           SendMessageW(form->auth, CB_GETCURSEL, 0, 0) == 0;
}
void updateAuthentication(HWND hwnd, Form *form) {
    bool ssh = SendMessageW(form->fields[Protocol - Name], CB_GETCURSEL, 0, 0) == 0;
    bool password = passwordMode(form);
    EnableWindow(form->auth, ssh);
    ShowWindow(form->password, password ? SW_SHOW : SW_HIDE);
    ShowWindow(form->remember, password ? SW_SHOW : SW_HIDE);
    ShowWindow(form->fields[Key - Name], !password ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, Browse), !password ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, KeyManager), ssh && !password ? SW_SHOW : SW_HIDE);
    EnableWindow(form->fields[Key - Name], ssh); EnableWindow(GetDlgItem(hwnd, Browse), ssh);
    EnableWindow(form->password, SendMessageW(form->remember, BM_GETCHECK, 0, 0) == BST_CHECKED);
    InvalidateRect(hwnd, nullptr, TRUE);
}
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
        form->auth = ui::control(hwnd, L"COMBOBOX", L"", Auth, CBS_DROPDOWNLIST | WS_TABSTOP);
        ui::place(form->auth, 28, 374, 350, 160);
        for (auto label : {L"Password", L"Public key authentication"}) SendMessageW(form->auth, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(form->auth, CB_SETCURSEL, form->profile.keyFile.empty() ? 0 : 1, 0);
        add(FontSize, L"11", 402, 381, 106, std::to_wstring(form->profile.fontSize));
        add(Key, L"Select a .ppk key file", 32, 455, 381, form->profile.keyFile);
        ui::place(ui::button(hwnd, L"Browse", Browse), 423, 448, 92, 38);
        form->password = ui::control(hwnd, L"EDIT", L"", Password, ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP);
        ui::place(form->password, 32, 455, 475, 25);
        SendMessageW(form->password, EM_SETPASSWORDCHAR, 0x25CF, 0);
        SendMessageW(form->password, EM_SETLIMITTEXT, 1024, 0);
        SendMessageW(form->password, EM_SETCUEBANNER, TRUE, (LPARAM)(form->profile.passwordSaved ? L"Saved — leave blank to keep" : L"Enter password to save"));
        form->remember = ui::checkbox(hwnd, L"Save password encrypted on this PC", Remember);
        ui::place(ui::button(hwnd, L"Generate or import a key pair", KeyManager), 28, 503, 488, 32);
        ui::place(form->remember, 28, 503, 490, 30);
        SendMessageW(form->remember, BM_SETCHECK, form->profile.passwordSaved ? BST_CHECKED : BST_UNCHECKED, 0);
        form->alias = ui::edit(hwnd, L"Optional tab label", Alias);
        ui::place(form->alias, 32, 623, 267, 25); SetWindowTextW(form->alias, form->profile.alias.c_str());
        form->color = ui::control(hwnd, L"COMBOBOX", L"", TabColor, CBS_DROPDOWNLIST | WS_TABSTOP);
        ui::place(form->color, 320, 618, 196, 260);
        const wchar_t *colorNames[] = {L"Default", L"Orange", L"Red", L"Yellow", L"Green", L"Blue", L"Purple", L"Pink", L"Gray"};
        int selectedColor = 0;
        for (int i = 0; i < 9; ++i) { SendMessageW(form->color, CB_ADDSTRING, 0, (LPARAM)colorNames[i]); if (form->profile.tabColor == colors[i]) selectedColor = i; }
        SendMessageW(form->color, CB_SETCURSEL, selectedColor, 0);
        ui::place(ui::button(hwnd, L"Cancel", Cancel), 288, 680, 100, 40);
        ui::place(ui::button(hwnd, L"Save host", Save), 398, 680, 118, 40);
        updateAuthentication(hwnd, form);
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
        if (form->profile.name == L"Password SSH test") {
            SendMessageW(form->remember, BM_SETCHECK, BST_CHECKED, 0); updateAuthentication(hwnd, form);
            auto fixture = testPassword(); SetWindowTextW(form->password, fixture.c_str());
            SecureZeroMemory(fixture.data(), fixture.size() * sizeof(wchar_t));
            captureTestWindow(hwnd, L"ui-password-host.bmp");
        } else captureTestWindow(hwnd, form->profile.keyFile.empty() ? L"ui-host-editor.bmp" : L"ui-public-key-host.bmp");
        PostMessageW(hwnd, WM_COMMAND, Save, 0);
        return 0;
#endif
    case WM_PAINT: {
        PAINTSTRUCT ps; auto dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, form->existing ? L"Edit host" : L"A new connection", ui::rect(28, 17, 490, 34), ui::TextSize::title, ui::bright, true);
        const struct { const wchar_t *text; int x, y; } labels[] = {
            {L"NAME",28,61}, {L"ADDRESS / SERIAL PORT",28,135}, {L"PROTOCOL",28,206}, {L"PORT / BAUD",276,206},
            {L"USERNAME",28,278}, {L"GROUP",276,278}, {L"AUTHENTICATION",28,351}, {L"FONT SIZE",398,351},
            {passwordMode(form) ? L"PASSWORD" : L"PRIVATE KEY (.ppk)",28,425}, {L"ALIAS (OPTIONAL)",28,595}, {L"TAB COLOR",320,595}
        };
        for (auto l : labels) ui::label(dc, l.text, ui::rect(l.x, l.y, 230, 20), ui::TextSize::caption, ui::muted, true);
        const RECT boxes[] = {ui::rect(27,83,489,41), ui::rect(27,157,489,41), ui::rect(275,228,241,41),
            ui::rect(27,300,239,41), ui::rect(275,300,241,41), ui::rect(397,373,119,41),
            ui::rect(27,447,passwordMode(form) ? 489 : 390,41), ui::rect(27,615,280,41)};
        for (auto b : boxes) ui::round(dc, b, ui::raised);
        std::wstring hint = L"Register the matching public key (.pub) on the server. The private key signs in; a public key alone cannot authenticate.";
        if (passwordMode(form)) {
            bool remember = SendMessageW(form->remember, BM_GETCHECK, 0, 0) == BST_CHECKED;
            hint = !remember ? L"Ask in the terminal each time. Saving removes any stored password." :
                form->profile.passwordSaved ? L"Leave blank to keep the saved password. Uncheck to remove it." :
                L"Protected for your Windows account. Excluded from settings backups.";
        }
        ui::label(dc, hint, ui::rect(28,550,490,38), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_WORDBREAK);
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
            updateAuthentication(hwnd, form);
        } else if ((LOWORD(wp) == Auth && HIWORD(wp) == CBN_SELCHANGE) || LOWORD(wp) == Remember) {
            updateAuthentication(hwnd, form);
        } else if (LOWORD(wp) == KeyManager) {
            auto path = showKeyManager(hwnd);
            if (!path.empty()) SetWindowTextW(form->fields[Key - Name], path.c_str());
        } else if (LOWORD(wp) == Browse) {
            wchar_t path[32768]{}; OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner = hwnd; ofn.lpstrFile = path;
            ofn.nMaxFile = 32768; ofn.lpstrFilter = L"PuTTY private keys (*.ppk)\0*.ppk\0All files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) SetWindowTextW(form->fields[Key - Name], path);
        } else if (LOWORD(wp) == Save || LOWORD(wp) == IDOK) {
            try {
                auto p = form->profile;
                p.alias = wook::trim(ui::value(form->alias));
                p.tabColor = colors[std::clamp((int)SendMessageW(form->color, CB_GETCURSEL, 0, 0), 0, 8)];
                p.name = wook::trim(ui::value(form->fields[Name - Name])); p.host = wook::trim(ui::value(form->fields[Host - Name]));
                if (p.host.starts_with(L"[") && p.host.ends_with(L"]")) p.host = p.host.substr(1, p.host.size() - 2);
                p.user = wook::trim(ui::value(form->fields[User - Name])); p.group = wook::trim(ui::value(form->fields[Group - Name]));
                p.keyFile = passwordMode(form) ? L"" : wook::trim(ui::value(form->fields[Key - Name]));
                p.protocol = protocols[SendMessageW(form->fields[Protocol - Name], CB_GETCURSEL, 0, 0)];
                if (p.protocol == L"ssh" && !passwordMode(form)) {
                    if (p.keyFile.empty()) throw std::runtime_error("Public key authentication needs the matching private key. Generate or import a key pair in Tools > SSH key manager.");
                    if (p.keyFile.size() >= 4 && _wcsicmp(p.keyFile.c_str() + p.keyFile.size() - 4, L".pub") == 0)
                        throw std::runtime_error("A .pub file belongs on the server in ~/.ssh/authorized_keys. Select its matching private .ppk key here. SSH certificates can be set in Connection settings > SSH > Auth > Credentials.");
                }
                auto portText = ui::value(form->fields[Port - Name]), fontText = ui::value(form->fields[FontSize - Name]);
                if (portText.empty() || fontText.empty() || portText.find_first_not_of(L"0123456789") != std::wstring::npos || fontText.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Port and font size must be numbers.");
                p.port = std::stoi(portText); p.fontSize = std::stoi(fontText);
                struct SecretInput {
                    std::wstring text;
                    ~SecretInput() { if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t)); }
                } password{ui::value(form->password)};
                bool remember = passwordMode(form) && SendMessageW(form->remember, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (remember && password.text.empty() && !form->profile.passwordSaved)
                    throw std::runtime_error("Enter the password to save, or turn off password storage.");
                auto action = !remember ? wook::PasswordAction::forget : password.text.empty() ? wook::PasswordAction::keep : wook::PasswordAction::replace;
                if (action == wook::PasswordAction::keep &&
                    (p.host != form->profile.host || p.port != form->profile.port || p.user != form->profile.user))
                    throw std::runtime_error("The server or username changed. Re-enter the password, or turn off password storage.");
                wook::saveProfile(p, form->existing ? form->profile.name : L"", action, password.text);
                p.passwordSaved = remember;
                form->profile = p; form->accepted = true; DestroyWindow(hwnd);
            } catch (const std::exception &e) { ui::error(hwnd, e); }
        } else if (LOWORD(wp) == Cancel || LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: SetWindowTextW(form->password, L""); form->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
bool editHost(HWND owner, wook::Profile &profile, bool existing) {
    WNDCLASSW wc{}; wc.lpfnWndProc = dialogProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WookHostEditor"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    Form form; form.profile = profile; form.existing = existing;
    if (!existing) form.profile.passwordSaved = false;
    RECT r{0,0,ui::px(545),ui::px(740)}; AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, 0, ui::dpi);
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
        L"wShell " WSHELL_VERSION_WIDE L"\nA quiet workspace for your servers.\n\n"
        L"Native Windows x64 · Portable · MIT License\n\n"
        L"Created by Hyunwook Park\n\n"
        L"PuTTY 0.85 — modified portable build (MIT)\nFlexoki — Steph Ango (MIT)\nJetBrains Mono — SIL OFL 1.1\n\n"
        L"Full notices are embedded: Tools → Open-source licenses.\n"
        L"Independent project; not affiliated with PuTTY or Termius.\n\n"
        L"Ctrl+Shift+T  New connection\nCtrl+Shift+D  Duplicate tab\nCtrl+Tab  Next tab\nCtrl+Shift+W  Close tab\n"
        L"Ctrl+Shift+P  Find hosts\nCtrl+Shift+C / V  Copy / paste\nAlt+1…9  Switch tabs\nF11  Full screen",
        L"About wShell", MB_OK | MB_ICONINFORMATION);
}
