#include "core.hpp"
#include "ui.hpp"
#include "keys.h"
#include "key_dialog.hpp"
#include <commdlg.h>
#include <thread>
#include <fstream>
#include <filesystem>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#endif

namespace {
enum { Algorithm = 501, Generate, ImportKey, Passphrase, PublicKey, Fingerprint, SavePrivate, SavePublic, KeyClose };
struct KeyForm {
    WsKey *key = nullptr, *next = nullptr;
    bool busy = false, done = false;
    std::thread worker;
    std::wstring error, status;
#ifdef WOOK_UI_TEST
    int testTicks = 0;
#endif
    ~KeyForm() { if (worker.joinable()) worker.join(); wsKeyFree(next); wsKeyFree(key); }
};
std::wstring chooseFile(HWND hwnd, bool save, bool pub = false) {
    wchar_t path[32768]{};
    if (save) wcscpy_s(path, pub ? L"wshell-key.pub" : L"wshell-key.ppk");
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = hwnd; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.lpstrFilter = pub ? L"OpenSSH public key (*.pub)\0*.pub\0\0" : L"SSH private key (*.ppk; id_*)\0*.ppk;id_*\0All files\0*.*\0\0";
    dialog.lpstrDefExt = pub ? L"pub" : L"ppk";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) ? path : L"";
}
void setBusy(HWND hwnd, KeyForm *form, bool busy) {
    form->busy = busy;
    for (int id : {Algorithm, Generate, ImportKey, Passphrase, KeyClose}) EnableWindow(GetDlgItem(hwnd, id), !busy);
    for (int id : {SavePrivate, SavePublic}) EnableWindow(GetDlgItem(hwnd, id), !busy && form->key);
    InvalidateRect(hwnd, nullptr, TRUE);
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
        auto algorithm = ui::control(hwnd, L"COMBOBOX", L"", Algorithm, CBS_DROPDOWNLIST | WS_TABSTOP);
        for (auto label : {L"Ed25519", L"RSA 3072", L"RSA 4096"}) SendMessageW(algorithm, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(algorithm, CB_SETCURSEL, 0, 0); ui::place(algorithm, 28, 108, 206, 180);
        ui::place(ui::button(hwnd, L"Generate", Generate), 249, 103, 143, 38);
        ui::place(ui::button(hwnd, L"Import key…", ImportKey), 406, 103, 166, 38);
        auto pass = ui::edit(hwnd, L"Optional: encrypts the saved private key", Passphrase);
        SetWindowLongPtrW(pass, GWL_STYLE, GetWindowLongPtrW(pass, GWL_STYLE) | ES_PASSWORD);
        SendMessageW(pass, EM_SETPASSWORDCHAR, 0x25CF, 0); ui::place(pass, 30, 199, 540, 28);
        auto fingerprint = ui::edit(hwnd, L"Key fingerprint", Fingerprint); SendMessageW(fingerprint, EM_SETREADONLY, TRUE, 0);
        SendMessageW(fingerprint, WM_SETFONT, (WPARAM)ui::font(ui::TextSize::caption), TRUE);
        ui::place(fingerprint, 30, 286, 540, 27);
        auto pub = ui::control(hwnd, L"EDIT", L"", PublicKey, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);
        ui::place(pub, 30, 355, 540, 94);
        ui::place(ui::button(hwnd, L"Save private key", SavePrivate), 28, 480, 210, 39);
        ui::place(ui::button(hwnd, L"Save public key", SavePublic), 250, 480, 210, 39);
        ui::place(ui::button(hwnd, L"Close", KeyClose), 472, 480, 100, 39);
        setBusy(hwnd, form, false);
#ifdef WOOK_UI_TEST
        SetTimer(hwnd, 91, 300, nullptr);
#endif
        return 0;
    }
    if (!form) return DefWindowProcW(hwnd, msg, wp, lp);
#ifdef WOOK_UI_TEST
    if (msg == WM_TIMER) {
        if (++form->testTicks == 1) PostMessageW(hwnd, WM_COMMAND, Generate, 0);
        if (form->key && !form->busy) {
            captureTestWindow(hwnd, L"ui-key-manager.bmp"); KillTimer(hwnd, 91); PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (form->testTicks > 50 && !form->busy) { KillTimer(hwnd, 91); DestroyWindow(hwnd); }
        return 0;
    }
#endif
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; auto dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r); ui::fill(dc, r, ui::panel);
        ui::label(dc, L"SSH keys. Made here.", ui::rect(28, 20, 550, 34), ui::TextSize::title, ui::bright, true);
        ui::label(dc, L"Create, import and export keys inside wShell.", ui::rect(28, 59, 550, 25), ui::TextSize::body, ui::muted);
        ui::label(dc, L"PASSPHRASE (FOR IMPORT / SAVE)", ui::rect(28, 162, 540, 27), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"SHA-256 FINGERPRINT", ui::rect(28, 247, 540, 27), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"PUBLIC KEY · authorized_keys", ui::rect(28, 322, 540, 25), ui::TextSize::caption, ui::muted, true);
        for (auto box : {ui::rect(27, 191, 546, 42), ui::rect(27, 278, 546, 40), ui::rect(27, 347, 546, 110)})
            ui::round(dc, box, ui::raised);
        ui::label(dc, form->busy ? L"Working…" : form->status, ui::rect(28, 536, 550, 30), ui::TextSize::body, ui::accent);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::raised);
        static HBRUSH brush = CreateSolidBrush(ui::raised); return (LRESULT)brush;
    }
    if (msg == WM_DRAWITEM) { ui::drawButton((DRAWITEMSTRUCT *)lp, ((DRAWITEMSTRUCT *)lp)->CtlID == Generate); return TRUE; }
    if (msg == WM_APP + 80) {
        if (form->worker.joinable()) form->worker.join();
        if (form->next) { wsKeyFree(form->key); form->key = form->next; form->next = nullptr; refreshKey(hwnd, form); }
        setBusy(hwnd, form, false);
        if (!form->error.empty()) MessageBoxW(hwnd, form->error.c_str(), L"wShell · Key manager", MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    if (msg == WM_COMMAND && !form->busy) {
        int id = LOWORD(wp);
        if (id == KeyClose || id == IDCANCEL) { DestroyWindow(hwnd); return 0; }
        try {
            if (id == Generate || id == ImportKey || id == SavePrivate) {
                auto path = id == Generate ? L"" : chooseFile(hwnd, id == SavePrivate);
                if (id != Generate && path.empty()) return 0;
                std::string secret;
                if (id != Generate) {
                    auto phrase = ui::value(GetDlgItem(hwnd, Passphrase)); secret = wook::utf8(phrase);
                    SecureZeroMemory(phrase.data(), phrase.size() * sizeof(wchar_t));
                    SetWindowTextW(GetDlgItem(hwnd, Passphrase), L"");
                }
                const int bits[] = {0, 3072, 4096};
                int choice = (int)SendMessageW(GetDlgItem(hwnd, Algorithm), CB_GETCURSEL, 0, 0);
                form->error.clear(); form->status.clear(); setBusy(hwnd, form, true);
                form->worker = std::thread([form, hwnd, id, path, rsaBits = bits[std::clamp(choice, 0, 2)], secret = std::move(secret)]() mutable {
                    bool ok = true;
                    if (id == Generate) form->next = wsKeyGenerate(rsaBits);
                    else if (id == ImportKey) form->next = wsKeyLoad(path.c_str(), secret.c_str());
                    else ok = wsKeySave(form->key, path.c_str(), secret.c_str()) != 0;
                    if (id != SavePrivate) ok = form->next != nullptr;
                    if (!ok) form->error = wook::wide(wsKeyError());
                    else form->status = id == SavePrivate
                        ? (secret.empty() ? L"Private key saved without a passphrase." : L"Private key saved with passphrase encryption.")
                        : L"Key ready. Select and copy the public key above.";
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
void showKeyManager(HWND owner) {
    WNDCLASSW wc{}; wc.lpfnWndProc = keyProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShellKeys"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    KeyForm form; RECT r{0,0,ui::px(600),ui::px(585)};
    AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, 0, ui::dpi);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"wShell · SSH key manager", WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right-r.left, r.bottom-r.top, owner, nullptr, wc.hInstance, &form);
    if (!window) return;
    EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOWNORMAL); MSG msg;
    while (!form.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
}
