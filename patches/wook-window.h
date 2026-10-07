/* Host integration and an optional saved-password provider. SSH crypto is unchanged. */
#include "wshell-credentials.h"
#include "wshell-ime.h"
static void wookImeSend(void *context, const wchar_t *text, int length) {
    WinGuiSeat *wgs = context;
    if (wgs->ldisc) term_keyinputw(wgs->term, text, length);
}
static void wookImePosition(WinGuiSeat *wgs) {
    wsImePosition(wgs->wshell_ime, wgs->caret_x, wgs->caret_y,
        wgs->font_width, wgs->font_height, wgs->fonts[FONT_NORMAL],
        wgs->colours[ATTR_DEFFG >> ATTR_FGSHIFT], wgs->colours[ATTR_DEFBG >> ATTR_BGSHIFT]);
}
static bool wookImeMessage(WinGuiSeat *wgs, UINT message, WPARAM wParam, LPARAM *lParam) {
    if (!wgs || !wgs->wshell_ime) return false;
    switch (message) {
      case WM_IME_SETCONTEXT:
        *lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
        break; /* DefWindowProc must still activate the OS candidate UI. */
      case WM_IME_STARTCOMPOSITION:
        wookImePosition(wgs); wsImeStart(wgs->wshell_ime); return true;
      case WM_IME_COMPOSITION:
        wookImePosition(wgs);
        wsImeComposition(wgs->wshell_ime, *lParam, wParam);
        return true; /* Do not let DefWindowProc generate a second WM_CHAR commit. */
      case WM_IME_ENDCOMPOSITION:
        wsImeClear(wgs->wshell_ime); return true;
      case WM_IME_NOTIFY:
        if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE)
            wookImePosition(wgs);
        break;
      case WM_KILLFOCUS:
        wsImeFinish(wgs->wshell_ime);
        break;
    }
    return false;
}
static HWND wookParent = NULL;
#include "wook-sftp.h"
static bool wookPasswordTried = false;
static void wookResetPassword(void) { wookPasswordTried = false; }
extern void wshellLoadFonts(void);
static bool wookSavedPassword(Conf *conf, prompts_t *p) {
    if (wookPasswordTried || p->data || !p->wshell_password_host || !p->wshell_password_user ||
        p->n_prompts != 1 || p->prompts[0]->echo || p->from_server || !p->to_server ||
        conf_get_int(conf, CONF_protocol) != PROT_SSH || !filename_is_null(conf_get_filename(conf, CONF_keyfile)) ||
        stricmp(p->wshell_password_host, conf_get_str(conf, CONF_host)) ||
        p->wshell_password_port != conf_get_int(conf, CONF_port) ||
        strcmp(p->wshell_password_user, conf_get_str_ambi(conf, CONF_username, NULL))) return false;
    const char *session = getenv("WOOK_PASSWORD_SESSION");
    if (!session || !*session) return false;
    char *password = NULL;
    int status = wsLoadSavedPassword(session, p->wshell_password_host, p->wshell_password_port, p->wshell_password_user, &password);
    if (status == 0) return false;
    wookPasswordTried = true;
    if (status < 0) return false; /* Corrupt or unavailable credentials fall back to the terminal prompt. */
    prompt_set_result(p->prompts[0], password);
    wsPasswordFree(password);
    return true;
}
static void wookIndependentConf(Conf *conf) {
    conf_set_bool(conf, CONF_tryagent, false);
    conf_set_bool(conf, CONF_agentfwd, false);
    conf_set_bool(conf, CONF_ssh_connection_sharing, false);
}
static void wookWindowInit(void) {
    wchar_t text[64];
    if (GetEnvironmentVariableW(L"WOOK_PARENT_HWND", text, 64)) {
        wookParent = (HWND)(uintptr_t)wcstoull(text, NULL, 10);
        DWORD pid = 0;
        GetWindowThreadProcessId(wookParent, &pid);
        if (!GetEnvironmentVariableW(L"WOOK_PARENT_PID", text, 64) ||
            pid != wcstoul(text, NULL, 10) || !IsWindow(wookParent)) exit(1);
    }
    wshellLoadFonts();
}
static void wookWindowAttach(HWND hwnd) {
    if (wookSftp) { SetTimer(hwnd, 0x57534654, 10, NULL); return; }
    if (!wookParent) return;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, 0);
    SetParent(hwnd, wookParent);
    SetWindowPos(hwnd, NULL, 0, 0, 600, 400, SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    PostMessageW(wookParent, WM_APP + 42, (WPARAM)hwnd, GetCurrentProcessId());
}
static bool wookKey(HWND hwnd, UINT message, WPARAM key, LPARAM flags) {
    if (!wookParent) return false;
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN && message != WM_KEYUP && message != WM_SYSKEYUP) return false;
    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    int command = 0;
    if (control && key == VK_TAB) command = shift ? 6 : 5;
    else if (control && shift && key == 'T') command = 1;
    else if (control && shift && key == 'D') command = 2;
    else if (control && shift && key == 'W') command = 3;
    else if (control && shift && key == 'R') command = 4;
    else if (control && shift && key == 'P') command = 7;
    else if (control && shift && key == 'L') command = 10;
    else if (alt && key >= '1' && key <= '9') command = 20 + (int)(key - '1');
    else if (key == VK_F11) command = 8;
    else if (alt && key == VK_F4) command = 9;
    if (!command) return false;
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(flags & (1L << 30)))
        PostMessageW(wookParent, WM_APP + 43, command, (LPARAM)hwnd);
    return true;
}
