#include "core.hpp"
#include "ui.hpp"
#include "keys.h"
#include "key_dialog.hpp"
#include "key_import.hpp"
#include <commdlg.h>
#include <thread>
#include <fstream>
#include <filesystem>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#endif

namespace {
enum { Algorithm = 501, Generate, ImportKey, Passphrase, PublicKey, Fingerprint, SavePrivate, SavePublic, KeyClose,
       Library, LoadRegistered, RegisterKey, UseKey };
struct KeyForm {
    WsKey *key = nullptr, *next = nullptr;
    bool busy = false, done = false, selectForHost = false, useWhenReady = false;
    std::thread worker;
    std::wstring error, status, savedPath, nextPath, acceptedPath;
    std::vector<std::wstring> library;
#ifdef WOOK_UI_TEST
    int testTicks = 0;
#endif
    ~KeyForm() { if (worker.joinable()) worker.join(); wsKeyFree(next); wsKeyFree(key); }
};
std::wstring chooseFile(HWND hwnd, bool save, bool pub = false) {
    wchar_t path[32768]{};
    if (save) wcscpy_s(path, pub ? L"wshell-key.pub" : L"wshell-key.ppk");
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = hwnd; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = pub ? L"OpenSSH public key (*.pub)\0*.pub\0\0" : save ? L"PuTTY private key (*.ppk)\0*.ppk\0\0" : L"SSH private keys (*.ppk; *.pem; *.key; *.openssh; *.txt; id_*)\0*.ppk;*.pem;*.key;*.openssh;*.txt;id_*\0All files\0*.*\0\0";
    dialog.lpstrDefExt = pub ? L"pub" : L"ppk";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) ? path : L"";
}
void setBusy(HWND hwnd, KeyForm *form, bool busy) {
    form->busy = busy;
    for (int id : {Algorithm, Generate, ImportKey, Passphrase, KeyClose, Library}) EnableWindow(GetDlgItem(hwnd, id), !busy);
    for (int id : {SavePrivate, SavePublic}) EnableWindow(GetDlgItem(hwnd, id), !busy && form->key);
    EnableWindow(GetDlgItem(hwnd, RegisterKey), !busy && form->key && form->savedPath.empty());
    EnableWindow(GetDlgItem(hwnd, LoadRegistered), !busy && SendDlgItemMessageW(hwnd, Library, LB_GETCURSEL, 0, 0) != LB_ERR);
    EnableWindow(GetDlgItem(hwnd, UseKey), !busy && (form->key || !form->savedPath.empty()));
    InvalidateRect(hwnd, nullptr, TRUE);
}
void refreshLibrary(HWND hwnd, KeyForm *form) {
    auto list = GetDlgItem(hwnd, Library); SendMessageW(list, LB_RESETCONTENT, 0, 0);
    form->library.clear();
    try { form->library = registeredPrivateKeys(); }
    catch (const std::exception &error) { form->status = L"Cannot read the key library: " + wook::wide(error.what()); }
    for (size_t i = 0; i < form->library.size(); ++i) {
        auto label = registeredKeyName(form->library[i]);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (form->library[i] == form->savedPath) SendMessageW(list, LB_SETCURSEL, i, 0);
    }
}
void refreshKey(HWND hwnd, KeyForm *form) {
    auto publicText = wsKeyPublic(form->key), fingerprint = wsKeyFingerprint(form->key);
    SetWindowTextW(GetDlgItem(hwnd, PublicKey), publicText ? wook::wide(publicText).c_str() : L"");
    SetWindowTextW(GetDlgItem(hwnd, Fingerprint), fingerprint ? wook::wide(fingerprint).c_str() : L"");
    wsKeyStringFree(publicText); wsKeyStringFree(fingerprint);
}
LRESULT CALLBACK keyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *form = (KeyForm *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        form = (KeyForm *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)form);
        ui::dark(hwnd);
        ui::place(ui::control(hwnd, L"LISTBOX", L"Registered private keys", Library,
                             LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP), 20, 148, 212, 317);
        SendDlgItemMessageW(hwnd, Library, LB_SETITEMHEIGHT, 0, ui::px(34));
        ui::place(ui::button(hwnd, L"Import & register…", ImportKey), 20, 480, 212, 38);
        ui::place(ui::button(hwnd, L"Load selected key", LoadRegistered), 20, 527, 212, 34);
        auto algorithm = ui::control(hwnd, L"COMBOBOX", L"", Algorithm, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto label : {L"Ed25519", L"RSA 3072", L"RSA 4096"}) SendMessageW(algorithm, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(algorithm, CB_SETCURSEL, 0, 0); ui::place(algorithm, 268, 108, 206, 180);
        ui::place(ui::button(hwnd, L"Generate", Generate), 489, 103, 143, 38);
        auto pass = ui::edit(hwnd, L"Optional: encrypts the saved private key", Passphrase);
        SetWindowLongPtrW(pass, GWL_STYLE, GetWindowLongPtrW(pass, GWL_STYLE) | ES_PASSWORD);
        SendMessageW(pass, EM_SETPASSWORDCHAR, 0x25CF, 0); ui::place(pass, 270, 199, 540, 28);
        auto fingerprint = ui::edit(hwnd, L"Key fingerprint", Fingerprint); SendMessageW(fingerprint, EM_SETREADONLY, TRUE, 0);
        SendMessageW(fingerprint, WM_SETFONT, (WPARAM)ui::font(ui::TextSize::caption), TRUE);
        ui::place(fingerprint, 270, 286, 540, 27);
        auto pub = ui::control(hwnd, L"EDIT", L"", PublicKey, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
        ui::place(pub, 270, 355, 540, 94);
        ui::place(ui::button(hwnd, L"Export private", SavePrivate), 268, 480, 172, 39);
        ui::place(ui::button(hwnd, L"Export public", SavePublic), 452, 480, 172, 39);
        ui::place(ui::button(hwnd, L"Register key", RegisterKey), 636, 480, 180, 39);
        ui::place(ui::button(hwnd, L"Close", KeyClose), 706, 569, 110, 36);
        ui::place(ui::button(hwnd, L"Use key", UseKey), 584, 569, 110, 36);
        ShowWindow(GetDlgItem(hwnd, UseKey), form->selectForHost ? SW_SHOW : SW_HIDE);
        refreshLibrary(hwnd, form);
        setBusy(hwnd, form, false);
#ifdef WOOK_UI_TEST
        SetTimer(hwnd, 91, 300, nullptr);
#endif
        return 0;
    }
    if (!form) return DefWindowProcW(hwnd, msg, wp, lp);
#ifdef WOOK_UI_TEST
    if (msg == WM_TIMER) {
        if (form->selectForHost && !form->library.empty()) {
            KillTimer(hwnd, 91);
            SendDlgItemMessageW(hwnd, Library, LB_SETCURSEL, 0, 0);
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(Library, LBN_SELCHANGE), (LPARAM)GetDlgItem(hwnd, Library));
            captureTestWindow(hwnd, L"ui-key-library.bmp");
            PostMessageW(hwnd, WM_COMMAND, UseKey, 0); return 0;
        }
        if (++form->testTicks == 1) PostMessageW(hwnd, WM_COMMAND, Generate, 0);
        if (form->key && !form->busy) {
            if (!testRepaintPreservesControl(hwnd, GetDlgItem(hwnd, Fingerprint)))
                throw std::runtime_error("Key manager redraw must preserve the displayed fingerprint");
            captureTestWindow(hwnd, L"ui-key-manager.bmp"); KillTimer(hwnd, 91);
            if (form->selectForHost) PostMessageW(hwnd, WM_COMMAND, UseKey, 0);
            else PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (form->testTicks > 50 && !form->busy) { KillTimer(hwnd, 91); DestroyWindow(hwnd); }
        return 0;
    }
#endif
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; auto dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, L"SSH keys. Made here.", ui::rect(28, 20, 550, 34), ui::TextSize::title, ui::bright, true);
        ui::label(dc, L"Import PPK, OpenSSH or PEM private keys and reuse them for your hosts.", ui::rect(28, 59, 788, 25), ui::TextSize::caption, ui::muted);
        ui::label(dc, L"REGISTERED KEYS", ui::rect(20, 103, 212, 23), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"Stored on this PC", ui::rect(20, 126, 212, 20), ui::TextSize::caption, ui::muted);
        ui::fill(dc, ui::rect(248, 99, 1, 462), ui::line);
        ui::label(dc, L"PASSPHRASE (FOR IMPORT / EXPORT)", ui::rect(268, 162, 540, 27), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"SHA-256 FINGERPRINT", ui::rect(268, 247, 540, 27), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"PUBLIC KEY · authorized_keys", ui::rect(268, 322, 540, 25), ui::TextSize::caption, ui::muted, true);
        for (auto box : {ui::rect(267, 191, 546, 42), ui::rect(267, 278, 546, 40), ui::rect(267, 347, 546, 110)})
            ui::round(dc, box, ui::raised);
        ui::label(dc, form->busy ? L"Working…" : form->status, ui::rect(268, 528, 550, 36), ui::TextSize::caption, ui::accent, false, DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::raised);
        static HBRUSH brush = CreateSolidBrush(ui::raised); return (LRESULT)brush;
    }
    if (msg == WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)lp)->CtlID == Library) {
        ((MEASUREITEMSTRUCT *)lp)->itemHeight = ui::px(34); return TRUE;
    }
    if (msg == WM_DRAWITEM) {
        auto item = (DRAWITEMSTRUCT *)lp;
        if (item->CtlID == Library) {
            bool selected = (item->itemState & ODS_SELECTED) != 0;
            ui::fill(item->hDC, item->rcItem, selected ? ui::line : ui::raised);
            if (selected) { RECT bar = item->rcItem; bar.right = bar.left + ui::px(3); ui::fill(item->hDC, bar, ui::accent); }
            if (item->itemID < form->library.size()) {
                RECT label = item->rcItem; label.left += ui::px(12); label.right -= ui::px(8);
                ui::label(item->hDC, registeredKeyName(form->library[item->itemID]), label, ui::TextSize::caption,
                          item->itemState & ODS_DISABLED ? ui::muted : selected ? ui::bright : ui::text);
            }
            if (item->itemState & ODS_FOCUS) { RECT focus = item->rcItem; InflateRect(&focus, -ui::px(5), -ui::px(3)); DrawFocusRect(item->hDC, &focus); }
        } else ui::drawButton(item, item->CtlID == Generate);
        return TRUE;
    }
    if (msg == WM_APP + 80) {
        if (form->worker.joinable()) form->worker.join();
        if (form->next) { wsKeyFree(form->key); form->key = form->next; form->next = nullptr; form->savedPath = form->nextPath; refreshKey(hwnd, form); }
        else if (!form->nextPath.empty()) form->savedPath = form->nextPath;
        refreshLibrary(hwnd, form);
        setBusy(hwnd, form, false);
        if (!form->error.empty()) wsMessageBoxW(hwnd, form->error.c_str(), L"wShell · Key manager", MB_OK | MB_ICONEXCLAMATION);
        if (form->useWhenReady && form->error.empty() && !form->savedPath.empty()) {
            form->acceptedPath = form->savedPath; DestroyWindow(hwnd); return 0;
        }
        form->useWhenReady = false;
        return 0;
    }
    if (msg == WM_COMMAND && !form->busy) {
        int id = LOWORD(wp);
        if (id == KeyClose || id == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        try {
            if (id == Library && HIWORD(wp) == LBN_SELCHANGE) {
                auto index = SendDlgItemMessageW(hwnd, Library, LB_GETCURSEL, 0, 0);
                if (index >= 0 && (size_t)index < form->library.size()) {
                    wsKeyFree(form->key); form->key = nullptr; form->savedPath = form->library[index]; refreshKey(hwnd, form);
                    form->status = form->selectForHost ? L"Select Load to inspect this key, or Use key to attach it to the host." : L"Select Load to inspect or export this registered key.";
                    setBusy(hwnd, form, false);
                }
                return 0;
            }
            if (id == UseKey) {
                if (!form->savedPath.empty()) { form->acceptedPath = form->savedPath; DestroyWindow(hwnd); return 0; }
                if (!form->key) return 0;
                // Register generated keys before returning a persistent path.
                form->useWhenReady = true; id = RegisterKey;
            }
            if (id == Generate || id == ImportKey || id == LoadRegistered || id == SavePrivate || id == RegisterKey) {
                auto path = id == LoadRegistered ? form->savedPath :
                    id == Generate || id == RegisterKey ? std::wstring() : chooseFile(hwnd, id == SavePrivate);
                if (id != Generate && id != RegisterKey && path.empty()) return 0;
                if ((id == RegisterKey || id == SavePrivate) && !form->key) return 0;
                std::string secret;
                if (id != Generate) {
                    auto phrase = ui::value(GetDlgItem(hwnd, Passphrase)); secret = wook::utf8(phrase);
                    SecureZeroMemory(phrase.data(), phrase.size() * sizeof(wchar_t));
                    SetWindowTextW(GetDlgItem(hwnd, Passphrase), L"");
                }
                const int bits[] = {0, 3072, 4096};
                int choice = (int)SendMessageW(GetDlgItem(hwnd, Algorithm), CB_GETCURSEL, 0, 0);
                form->error.clear(); form->status.clear(); form->nextPath.clear(); setBusy(hwnd, form, true);
                form->worker = std::thread([form, hwnd, id, path, rsaBits = bits[std::clamp(choice, 0, 2)], secret = std::move(secret)]() mutable {
                    try {
                        if (id == Generate) form->next = wsKeyGenerate(rsaBits);
                        else if (id == ImportKey || id == LoadRegistered) form->next = wsKeyLoad(path.c_str(), secret.c_str());
                        if ((id == Generate || id == ImportKey || id == LoadRegistered) && !form->next) throw std::runtime_error(wsKeyError());
                        if (id == SavePrivate && !wsKeySave(form->key, path.c_str(), secret.c_str())) throw std::runtime_error(wsKeyError());
                        if (id == ImportKey || id == RegisterKey)
                            form->nextPath = registerPrivateKey(id == ImportKey ? form->next : form->key, id == ImportKey ? path : L"SSH key", secret.c_str());
                        else if (id == SavePrivate || id == LoadRegistered) form->nextPath = path;
                        form->status = id == ImportKey || id == RegisterKey ? L"Key registered on this PC. Private keys are excluded from settings backups." :
                            id == SavePrivate ? (secret.empty() ? L"Private key exported without a passphrase." : L"Private key exported with passphrase encryption.") :
                            L"Key ready. Select and copy the public key above.";
                    } catch (const std::exception &error) {
                        wsKeyFree(form->next); form->next = nullptr; form->error = wook::wide(error.what());
                    }
                    SecureZeroMemory(secret.data(), secret.size()); PostMessageW(hwnd, WM_APP + 80, 0, 0);
                });
            } else if (id == SavePublic && form->key) {
                auto path = chooseFile(hwnd, true, true); if (path.empty()) return 0;
                auto text = wsKeyPublic(form->key);
                std::ofstream out(std::filesystem::path(path), std::ios::binary); if (text) out << text << '\n';
                wsKeyStringFree(text); out.close();
                if (!out) throw std::runtime_error("Could not save the public key.");
                form->status = L"Public key saved."; InvalidateRect(hwnd, nullptr, TRUE);
            }
        } catch (const std::exception &e) { setBusy(hwnd, form, false); ui::error(hwnd, e); }
        return 0;
    }
    if (msg == WM_CLOSE) { if (!form->busy) DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) { form->done = true; return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
std::wstring showKeyManager(HWND owner, bool selectForHost) {
    WNDCLASSW wc{}; wc.lpfnWndProc = keyProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShellKeys"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    KeyForm form; form.selectForHost = selectForHost;
    RECT r{0,0,ui::px(844),ui::px(620)};
    AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, 0, ui::dpi);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"wShell · SSH key manager", WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right-r.left, r.bottom-r.top, owner, nullptr, wc.hInstance, &form);
    if (!window) return L"";
    EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOWNORMAL); MSG msg;
    while (!form.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return form.acceptedPath;
}
