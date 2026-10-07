/* Host integration only: no terminal, authentication, or crypto changes. */
static HWND wookParent = NULL;
extern void wshellLoadFonts(void);
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
    if (!wookParent) return;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_CHILD | WS_CLIPSIBLINGS;
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
    else if (alt && key >= '1' && key <= '9') command = 20 + (int)(key - '1');
    else if (key == VK_F11) command = 8;
    else if (alt && key == VK_F4) command = 9;
    if (!command) return false;
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(flags & (1L << 30)))
        PostMessageW(wookParent, WM_APP + 43, command, (LPARAM)hwnd);
    return true;
}
