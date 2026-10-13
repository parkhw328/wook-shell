// Real loopback keyboard-interactive authentication in the separate SFTP engine.
// SendInput exercises foreground keyboard routing, not just a posted WM_COMMAND.
template<typename Check> bool testSftpAuthentication(App &app, Check require) {
    static int step = 0, scenario = 0, replies = 0, waits = 0;
    static HWND background{};
    auto focus = [&] {
        GUITHREADINFO info{sizeof(info)};
        require(GetGUIThreadInfo(0, &info) != FALSE, "Cannot inspect authentication keyboard focus");
        return info.hwndFocus;
    };
    auto prompt = [&] {
        struct Search { HWND owner, dialog{}; } search{app.hwnd};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(data);
            wchar_t kind[80]{}; GetClassNameW(window, kind, 80);
            if (GetWindow(window, GW_OWNER) == search.owner && IsWindowVisible(window) &&
                !wcscmp(kind, L"wShell.TextPrompt")) { search.dialog = window; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.dialog;
    };
    auto key = [&](WORD code, bool unicode = false) {
        INPUT events[2]{};
        for (auto &event : events) {
            event.type = INPUT_KEYBOARD;
            if (unicode) { event.ki.wScan = code; event.ki.dwFlags = KEYEVENTF_UNICODE; }
            else event.ki.wVk = code;
        }
        events[1].ki.dwFlags |= KEYEVENTF_KEYUP;
        require(SendInput(2, events, sizeof(INPUT)) == 2, "Authentication key injection failed");
    };
    auto workspace = [&] {
        require(GetForegroundWindow() == app.hwnd && IsWindowVisible(app.hwnd) && !IsIconic(app.hwnd),
                "Closing an authentication prompt must keep the workspace in the foreground");
        require(IsWindowEnabled(app.hwnd), "Authentication must leave the workspace enabled");
        require(focus() == GetDlgItem(app.tabs[0]->terminal, 15), "Authentication must return keyboard input to the SFTP list");
    };
    if (++waits > 60) throw std::runtime_error("SFTP authentication focus test timed out");
    if (step == 0) {
        // Activate only our own caption using the same OS input path as a user.
        ShowWindow(app.hwnd, SW_RESTORE);
        SetWindowPos(app.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RECT r{}; GetWindowRect(app.hwnd, &r);
        POINT point{r.left + (r.right - r.left) / 3, r.top + ui::px(16)};
        require(WindowFromPoint(point) == app.hwnd, "Authentication test click must hit its own window");
        INPUT events[3]{};
        for (auto &event : events) event.type = INPUT_MOUSE;
        events[0].mi.dx = MulDiv(point.x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
        events[0].mi.dy = MulDiv(point.y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
        events[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        events[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; events[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        require(SendInput(3, events, sizeof(INPUT)) == 3, "Authentication test caption click failed");
        step = 1; waits = 0;
    } else if (step == 1) {
        SetWindowPos(app.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        require(GetForegroundWindow() == app.hwnd, "Authentication regression needs an interactive foreground desktop");
        wchar_t port[32]{}, pin[200]{};
        require(GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32) &&
                GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", pin, 200), "Missing authentication server fixture");
        Profile p; p.name = L"SFTP authentication " + std::to_wstring(scenario);
        p.host = L"127.0.0.1"; p.port = _wtoi(port); p.user = scenario == 0 ? L"otp" : scenario == 1 ? L"cancel" : L"background";
        wook::saveProfile(p);
        auto path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
        require(store != nullptr, "Cannot open authentication fixture profile");
        wsSet(store, "SSHManualHostKeys", wook::utf8(pin).c_str());
        wsSet(store, "AuthKI", "1"); wsSet(store, "TryAgent", "0");
        int saved = wsSave(store); wsClose(store); require(saved != 0, "Cannot save authentication fixture profile");
        app.connect(p, true, false, false, true); replies = 0; step = 2; waits = 0;
    } else if (step == 2) {
        auto dialog = prompt(); if (!dialog || GetPropW(dialog, L"wShell.TestPromptDriven")) return false;
        DWORD pid{}; GetWindowThreadProcessId(dialog, &pid);
        require(pid != GetCurrentProcessId(), "SFTP authentication must exercise the actual worker process");
        require(GetForegroundWindow() == dialog && focus() == GetDlgItem(dialog, 10),
                "OTP input must go to the foreground authentication edit control");
        require(SendMessageW(GetDlgItem(dialog, 10), EM_GETPASSWORDCHAR, 0, 0) != 0, "OTP must remain masked");
        require(SetPropW(dialog, L"wShell.TestPromptDriven", (HANDLE)1) != FALSE, "Cannot track authentication prompt");
        if (scenario == 2) {
            // Switch to another test-owned top-level window before dismissing the prompt.
            background = CreateWindowExW(0, L"STATIC", L"Authentication background fixture", WS_OVERLAPPEDWINDOW,
                40, 40, 440, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            require(background != nullptr, "Cannot create background activation fixture");
            ShowWindow(background, SW_SHOW); SetForegroundWindow(background);
            step = 3;
        } else if (scenario == 1) {
            key(VK_ESCAPE); step = 4;
        } else {
            for (auto ch : replies == 0 ? std::wstring(L"123456") : std::wstring(L"654321")) key((WORD)ch, true);
            key(VK_RETURN);
            if (++replies == 2) step = 4;
        }
        waits = 0;
    } else if (step == 3) {
        require(GetForegroundWindow() == background, "Background fixture must acquire focus before prompt cancellation");
        auto dialog = prompt(); require(dialog != nullptr, "Background authentication prompt must remain open");
        PostMessageW(dialog, WM_CLOSE, 0, 0); step = 4; waits = 0;
    } else if (step == 4 && !app.tabs.empty() && !prompt()) {
        auto state = SendMessageW(app.tabs[0]->terminal, WM_APP + 121, 0, 0);
        if (!state) return false;
        require(state == (scenario == 0 ? 1 : -1), "Only completed OTP authentication may connect SFTP");
        if (scenario == 2) {
            require(GetForegroundWindow() == background, "Closing a background prompt must not steal foreground focus");
            DestroyWindow(background); background = nullptr; SetForegroundWindow(app.hwnd);
        } else {
            workspace();
            if (scenario == 0) {
                require(ListView_GetItemCount(GetDlgItem(app.tabs[0]->terminal, 15)) == 1,
                        "OTP success must populate the remote SFTP listing");
                captureTestWindow(app.hwnd, L"ui-sftp-authenticated.bmp");
            }
        }
        app.closeTab(0, true); step = 5; waits = 0;
    } else if (step == 5 && app.tabs.empty()) {
        if (++scenario == 3) return true;
        step = 1; waits = 0;
    }
    return false;
}
