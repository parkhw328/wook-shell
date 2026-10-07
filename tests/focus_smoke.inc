// Exercise real keyboard routing. Posting WM_CHAR directly to a terminal would
// bypass the focus bugs this regression test is meant to catch.
template<typename Check> bool testSplitFocus(App &app, Check require) {
    static int step = -1, layoutCount = 2, clickIndex = 0, samples = 0;
    static ULONGLONG cursorHash = 0;
    static COLORREF borderColor = 0;
    static bool cursorChanged = false, borderChanged = false;
    static std::vector<Tab *> navigationGroup;
    static int paneWidth = 0;
    static RECT caret{};
    const int clickTabs[] = {0, 1, 2, 3, 3}; // Also return to the already selected pane.
    auto focus = [&] {
        GUITHREADINFO info{sizeof(info)};
        require(GetGUIThreadInfo(0, &info) != FALSE, "Cannot inspect foreground keyboard focus");
        return info.hwndFocus;
    };
    auto type = [&](const std::wstring &text) {
        require(GetForegroundWindow() == app.hwnd && focus() == app.tabs[app.active]->terminal,
                "Native test input must be routed to the foreground test terminal");
        std::vector<INPUT> events;
        for (wchar_t ch : text) {
            INPUT down{}; down.type = INPUT_KEYBOARD;
            if (ch == L'\r') down.ki.wVk = VK_RETURN;
            else { down.ki.wScan = ch; down.ki.dwFlags = KEYEVENTF_UNICODE; }
            events.push_back(down); down.ki.dwFlags |= KEYEVENTF_KEYUP; events.push_back(down);
        }
        require(SendInput((UINT)events.size(), events.data(), sizeof(INPUT)) == events.size(), "Native keyboard injection failed");
    };
    auto marker = [&](const std::wstring &name) { return std::filesystem::path(app.directory) / (name + L".ready"); };
    auto sendMarker = [&](const std::wstring &name) { type(L"echo focus>\"" + marker(name).wstring() + L"\"\r"); };
    auto shortcut = [&](WORD key) {
        require(GetForegroundWindow() == app.hwnd, "Navigation shortcut must target the foreground test workspace");
        INPUT events[6]{};
        const WORD keys[] = {VK_CONTROL, VK_SHIFT, key, key, VK_SHIFT, VK_CONTROL};
        for (int i = 0; i < 6; ++i) { events[i].type = INPUT_KEYBOARD; events[i].ki.wVk = keys[i]; if (i >= 3) events[i].ki.dwFlags = KEYEVENTF_KEYUP; }
        require(SendInput(6, events, sizeof(INPUT)) == 6, "Navigation shortcut injection failed");
    };
    auto click = [&](HWND terminal) {
        RECT r{}; GetClientRect(terminal, &r);
        // The embedded terminal must acquire focus in its own mouse handler.
        auto point = MAKELPARAM(r.right / 2, r.bottom / 2);
        SendMessageW(terminal, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(terminal, WM_LBUTTONUP, 0, point);
    };
    auto nativeClick = [&](HWND target, POINT point) {
        require(WindowFromPoint(point) == target, "Native click must hit the test window, never another application");
        int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT events[3]{};
        for (auto &event : events) event.type = INPUT_MOUSE;
        events[0].mi.dx = MulDiv(point.x - left, 65535, width - 1);
        events[0].mi.dy = MulDiv(point.y - top, 65535, height - 1);
        events[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        events[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; events[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        require(SendInput(3, events, sizeof(INPUT)) == 3, "Native test click failed");
    };
    auto border = [&] {
        auto it = std::find(app.panes.begin(), app.panes.end(), app.tabs[app.active].get());
        auto r = app.paneRects[it - app.panes.begin()];
        UpdateWindow(app.hwnd);
        HDC dc = GetDC(app.hwnd); auto color = GetPixel(dc, ui::px(r.x + 1), ui::px(r.y + 1)); ReleaseDC(app.hwnd, dc);
        require(color != CLR_INVALID, "Cannot sample pane border"); return color;
    };
    auto cursor = [&] {
        auto terminal = app.tabs[app.active]->terminal;
        UpdateWindow(terminal); HDC dc = GetDC(terminal); ULONGLONG hash = 1469598103934665603ull;
        for (int y = caret.top; y < caret.bottom; ++y) for (int x = caret.left; x < caret.right; ++x)
            hash = (hash ^ GetPixel(dc, x, y)) * 1099511628211ull;
        ReleaseDC(terminal, dc); return hash;
    };
    { std::ofstream progress(std::filesystem::path(app.directory) / L"focus-progress.json");
      progress << "{\"step\":" << step << ",\"active\":" << app.active << ",\"foreground\":"
               << (uintptr_t)GetForegroundWindow() << ",\"workspace\":" << (uintptr_t)app.hwnd
               << ",\"focus\":" << (uintptr_t)focus() << "}\n"; }
    if (step == -1) {
        // A background test launcher cannot necessarily SetForegroundWindow.
        // Click only our own caption, then remove the temporary topmost status.
        ShowWindow(app.hwnd, SW_RESTORE);
        SetWindowPos(app.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RECT r{}; GetWindowRect(app.hwnd, &r);
        nativeClick(app.hwnd, {r.left + (r.right - r.left) / 3, r.top + ui::px(16)}); step = 0;
    } else if (step == 0) {
        SetWindowPos(app.hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        require(GetForegroundWindow() == app.hwnd, "Focus regression needs the test workspace in the foreground");
        app.select(0);
        SetFocus(app.control(Split)); app.setSplit(2);
        bool splitFocused = focus() == app.tabs[0]->terminal;
        auto afterSplit = focus();
        SetFocus(app.control(CommandText)); click(app.tabs[1]->terminal);
        bool clickFocused = focus() == app.tabs[1]->terminal;
        std::ofstream probe(marker(L"focus-probe").replace_extension(L".json"));
        probe << "{\"splitFocus\":" << (splitFocused ? "true" : "false")
              << ",\"clickFocus\":" << (clickFocused ? "true" : "false")
              << ",\"afterSplit\":" << (uintptr_t)afterSplit << ",\"afterClick\":" << (uintptr_t)focus()
              << ",\"first\":" << (uintptr_t)app.tabs[0]->terminal << ",\"second\":" << (uintptr_t)app.tabs[1]->terminal
              << ",\"active\":" << app.active << "}\n";
        require(splitFocused, "Choosing split must restore keyboard focus from the toolbar to the terminal");
        require(clickFocused, "Clicking a terminal body must take keyboard focus from the command field");
        app.select(0); step = 1;
    } else if (step == 1) {
        SetFocus(app.control(Split)); app.setSplit(layoutCount);
        require(focus() == app.tabs[app.active]->terminal, "Every split layout must restore terminal focus");
        sendMarker(L"focus-layout-" + std::to_wstring(layoutCount)); step = 2;
    } else if (step == 2 && std::filesystem::exists(marker(L"focus-layout-" + std::to_wstring(layoutCount)))) {
        if (++layoutCount <= 4) step = 1; else step = 3;
    } else if (step == 3) {
        SetWindowTextW(app.control(CommandText), L"draft stays here"); SetFocus(app.control(CommandText));
        auto target = app.tabs[clickTabs[clickIndex]]->terminal;
        RECT r{}; GetWindowRect(target, &r);
        nativeClick(target, {(r.left + r.right) / 2, (r.top + r.bottom) / 2}); step = 4;
    } else if (step == 4) {
        int target = clickTabs[clickIndex];
        require(focus() == app.tabs[target]->terminal && app.active == target, "Clicked pane must own both selection and keyboard focus");
        require(ui::value(app.control(CommandText)) == L"draft stays here", "Clicking a pane must preserve the command draft");
        sendMarker(L"focus-click-" + std::to_wstring(clickIndex)); step = 5;
    } else if (step == 5 && std::filesystem::exists(marker(L"focus-click-" + std::to_wstring(clickIndex)))) {
        require(ui::value(app.control(CommandText)) == L"draft stays here", "Terminal typing must not go into the command draft");
        if (++clickIndex < 5) step = 3;
        else {
            // Simulate delayed focus notifications from a previously active pane.
            PostMessageW(app.hwnd, WM_APP + 46, (WPARAM)app.tabs[0]->terminal, 0); step = 6;
        }
    } else if (step == 6) {
        require(app.active == 3 && focus() == app.tabs[3]->terminal, "Stale focus notification must not change the active pane");
        GUITHREADINFO info{sizeof(info)}; GetGUIThreadInfo(0, &info); caret = info.rcCaret;
        require(info.hwndCaret == app.tabs[3]->terminal && caret.right > caret.left && caret.bottom > caret.top,
                "Focused terminal must expose its cursor cell");
        cursorHash = cursor(); borderColor = border();
        captureTestWindow(app.hwnd, L"ui-split-focus.bmp"); step = 7;
    } else if (step == 7) {
        cursorChanged |= cursor() != cursorHash; borderChanged |= border() != borderColor;
        if (++samples >= 10) {
            UINT blinkTime = GetCaretBlinkTime();
            bool blinking = blinkTime && blinkTime != INFINITE;
            require(cursorChanged == blinking, "Focused terminal cursor must follow Windows blinking settings while idle");
            require(borderChanged == blinking, "Keyboard-focused pane border must follow Windows blinking settings");
            captureTestWindow(app.hwnd, L"ui-split-focus-pulse.bmp");
            SetFocus(app.control(CommandText)); samples = 0; step = 8;
        }
    } else if (step == 8) {
        require(focus() == app.control(CommandText), "Focus polling must not steal keyboard input from the command field");
        borderColor = border(); cursorHash = cursor(); captureTestWindow(app.hwnd, L"ui-split-command-focus.bmp"); step = 9;
    } else if (step == 9) {
        require(border() == borderColor, "Selected pane border must stop pulsing while editing a command draft");
        require(cursor() == cursorHash, "Unfocused terminal cursor must stop blinking");
        if (++samples >= 4) {
            app.select(3); SetFocus(app.control(Split)); app.zoomPane();
            require(focus() == app.tabs[3]->terminal, "Zoom must restore keyboard focus");
            SetFocus(app.control(Split)); app.zoomPane();
            require(focus() == app.tabs[3]->terminal, "Restoring split must restore keyboard focus");
            SetFocus(app.control(Split)); app.setSplit(1);
            require(focus() == app.tabs[3]->terminal, "Returning to single pane must restore keyboard focus");
            app.setSplit(4); navigationGroup = app.panes; paneWidth = app.paneRects[0].width;
            app.toggleTarget(app.panes[1]);
            SendMessageW(app.control(SendAll), BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(app.control(SyncInput), BM_CLICK, 0, 0);
            shortcut('H'); step = 10;
        }
    } else if (step == 10) {
        require(!app.navigationVisible && focus() == app.tabs[3]->terminal && app.paneRects[0].width > paneWidth,
                "Terminal Ctrl+Shift+H must hide navigation, reclaim space and retain terminal focus");
        require(app.panes == navigationGroup && !app.isTarget(app.panes[1]) &&
                SendMessageW(app.control(SendAll), BM_GETCHECK, 0, 0) == BST_CHECKED &&
                SendMessageW(app.control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED,
                "Hiding navigation must preserve pane identities, excluded targets and input modes");
        require(ui::value(app.control(CommandText)) == L"draft stays here", "Navigation toggle must preserve the command draft");
        captureTestWindow(app.hwnd, L"ui-split-navigation-collapsed.bmp");
        SendMessageW(app.control(SendAll), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(app.control(SyncInput), BM_CLICK, 0, 0);
        sendMarker(L"focus-navigation-hidden"); step = 11;
    } else if (step == 11 && std::filesystem::exists(marker(L"focus-navigation-hidden"))) {
        shortcut('P'); step = 12;
    } else if (step == 12) {
        require(app.navigationVisible && focus() == app.control(Search), "Terminal search shortcut must expand navigation and focus its field");
        shortcut('H'); step = 13;
    } else if (step == 13) {
        require(!app.navigationVisible && focus() == app.tabs[3]->terminal, "Hiding focused navigation must return input to the selected terminal");
        sendMarker(L"focus-navigation-return"); step = 14;
    } else if (step == 14 && std::filesystem::exists(marker(L"focus-navigation-return"))) {
        app.setNavigation(true); app.toggleTarget(app.panes[1]);
        require(focus() == app.tabs[3]->terminal && app.panes == navigationGroup, "Expanding navigation must preserve terminal focus and pane identity");
        SetWindowTextW(app.control(CommandText), L""); return true;
    }
    return false;
}
