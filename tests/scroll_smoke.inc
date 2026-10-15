// Observe the real terminal scrollbar while loopback SSH output continues.
// Wheel messages exercise the engine, not native OS pointer routing.
template<typename Check> bool testTerminalScroll(App &app, Check require) {
    static int step = 0, waits = 0, heldPosition = 0, previousBottom = 0;
    static RECT firstCell{};
    static const bool followOutput = GetEnvironmentVariableW(L"WOOK_TEST_FOLLOW_OUTPUT", nullptr, 0) != 0;
    static const bool localWheel = GetEnvironmentVariableW(L"WOOK_TEST_LOCAL_WHEEL", nullptr, 0) != 0;
    static const bool blockMouse = GetEnvironmentVariableW(L"WOOK_TEST_BLOCK_MOUSE", nullptr, 0) != 0;
    auto file = [&](const wchar_t *name) { return std::filesystem::path(app.directory) / name; };
    auto terminal = [&] { return app.tabs[0]->terminal; };
    auto scroll = [&] {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        require(GetScrollInfo(terminal(), SB_VERT, &info), "Terminal must expose its scrollback position");
        std::ofstream(file(L"scroll-positions.jsonl"), std::ios::app) << "{\"step\":" << step << ",\"position\":" << info.nPos
            << ",\"max\":" << info.nMax << ",\"page\":" << info.nPage << "}\n";
        return info;
    };
    auto bottom = [](const SCROLLINFO &info) { return info.nMax - (int)info.nPage + 1; };
    auto wheel = [&](int delta, bool shift = false) {
        RECT area{}; GetClientRect(terminal(), &area);
        POINT point{area.right / 2, area.bottom / 2}; ClientToScreen(terminal(), &point);
        SendMessageW(terminal(), WM_MOUSEWHEEL, MAKEWPARAM(shift ? MK_SHIFT : 0, delta), MAKELPARAM(point.x, point.y));
    };
    auto enter = [&](bool keypad) {
        LPARAM flags = 1 | (0x1c << 16) | (keypad ? (1 << 24) : 0);
        SendMessageW(terminal(), WM_KEYDOWN, VK_RETURN, flags);
        SendMessageW(terminal(), WM_KEYUP, VK_RETURN, flags | (LPARAM)0xc0000000u);
    };
    auto request = [&](const wchar_t *name, int next) {
        std::ofstream(file(name)) << "ready\n"; step = next; waits = 0;
    };
    { std::ofstream progress(file(L"scroll-progress.json")); progress << "{\"step\":" << step << "}\n"; }
    if (++waits > 40) throw std::runtime_error("Scroll regression timed out waiting for SSH output");
    if (step == 0) {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE); UpdateWindow(app.hwnd);
        wchar_t port[32]{}, fingerprint[200]{};
        require(GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32), "Missing scroll SSH port");
        require(GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", fingerprint, 200), "Missing scroll SSH host key");
        Profile p; p.name = L"Scroll regression"; p.user = L"scroll"; p.host = L"127.0.0.1"; p.port = _wtoi(port);
        wook::saveProfile(p);
        auto path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
        require(store != nullptr, "Cannot create scroll session");
        wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
        wsSet(store, "ScrollOnKey", "0"); // Enter must work for existing sessions with this disabled.
        wsSet(store, "ScrollBar", "1"); // Observe position via Win32 instead of a test-only engine hook.
        wsSet(store, "ScrollOnDisp", "1"); // Older saved sessions include the old engine default.
        if (blockMouse) wsSet(store, "NoMouseReporting", "1");
        if (followOutput) wsSet(store, "WookScrollOnDisp", "1");
        require(wsSave(store), "Cannot save scroll fixture"); wsClose(store);
        app.connect(p, true); step = 1; waits = 0;
    } else if (step == 1 && IsWindow(terminal()) && std::filesystem::exists(file(L"scroll-screen.ready")) && waits > 3) {
        app.focusActive();
        require(testDarkScrollbars(terminal(), require) == 1, "Embedded terminal must use a dark native scrollbar");
        auto info = scroll(); previousBottom = bottom(info);
        GUITHREADINFO thread{sizeof(thread)};
        require(GetGUIThreadInfo(GetWindowThreadProcessId(terminal(), nullptr), &thread), "Find terminal character-cell geometry");
        require(thread.hwndCaret == terminal(), "Character-cell geometry must belong to the test terminal");
        firstCell = thread.rcCaret;
        require(firstCell.right > firstCell.left && firstCell.bottom > firstCell.top, "Terminal must expose valid character-cell geometry");
        // Initial output ends at column one of the last row. Capture the grid
        // origin now: an unfocused terminal may keep a stale OS caret later.
        OffsetRect(&firstCell, 0, -(int)(info.nPage - 1) * (firstCell.bottom - firstCell.top));
        captureTestWindow(app.hwnd, L"ui-scroll-initial.bmp");
        SendMessageW(terminal(), WM_SYSCOMMAND, 0x0010, 0); // Actual engine Event Log.
        struct LogSearch { DWORD pid; HWND dialog{}; } search{app.tabs[0]->pid};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto &search = *reinterpret_cast<LogSearch *>(data);
            DWORD pid{}; GetWindowThreadProcessId(window, &pid);
            if (pid == search.pid && IsWindowVisible(window) && ui::value(window).ends_with(L"Event Log")) {
                search.dialog = window; return FALSE;
            }
            return TRUE;
        }, (LPARAM)&search);
        require(search.dialog != nullptr, "Terminal event log must open");
        auto log = FindWindowExW(search.dialog, nullptr, L"ListBox", nullptr);
        require(log && (testDarkScrollbars(log, require) & 1), "Engine event log must render its scrollbar dark");
        captureTestWindow(search.dialog, L"ui-scrollbar-event-log.bmp");
        SendMessageW(search.dialog, WM_CLOSE, 0, 0);
        require(previousBottom > 40 && info.nPos == previousBottom, "Fixture must start at the bottom with enough history");
        wheel(60); step = 2; waits = 0;
    } else if (step == 2 && waits > 2) {
        require(scroll().nPos == previousBottom, "Half a wheel notch must wait for the remaining delta");
        wheel(60); step = 3; waits = 0;
    } else if (step == 3 && waits > 2) {
        heldPosition = scroll().nPos;
        require(heldPosition < previousBottom, "Wheel up must show terminal history");
        captureTestWindow(app.hwnd, L"ui-scroll-before-output.bmp");
        request(L"scroll-output.request", 4);
    } else if (step == 4 && std::filesystem::exists(file(L"scroll-output.ready")) && waits > 3) {
        auto info = scroll();
        std::ofstream(file(L"scroll-observation.json")) << "{\"before\":" << heldPosition << ",\"after\":" << info.nPos
            << ",\"bottom\":" << bottom(info) << ",\"followOutput\":" << (followOutput ? "true" : "false") << "}\n";
        captureTestWindow(app.hwnd, L"ui-scroll-after-output.bmp");
        require(bottom(info) > previousBottom, "SSH output must continue adding history while scrolled up");
        require(info.nPos == (followOutput ? bottom(info) : heldPosition), "New output must preserve the viewed history unless follow-output is explicitly enabled");
        wheel(-WHEEL_DELTA * 20); step = 5; waits = 0;
    } else if (step == 5 && waits > 2) {
        require(scroll().nPos == bottom(scroll()), "Wheel down must return to the live terminal");
        previousBottom = bottom(scroll()); request(L"scroll-tail.request", 6);
    } else if (step == 6 && std::filesystem::exists(file(L"scroll-tail.ready")) && waits > 3) {
        auto info = scroll();
        require(bottom(info) > previousBottom && info.nPos == bottom(info), "Output must keep following once the user returns to the bottom");
        wheel(WHEEL_DELTA); step = 10; waits = 0;
    } else if (step == 10 && waits > 2) {
        require(scroll().nPos < bottom(scroll()), "Enter fixture must be viewing history");
        enter(false); step = 11; waits = 0;
    } else if (step == 11 && waits > 2) {
        require(scroll().nPos == bottom(scroll()), "Enter must return to live output without a server echo");
        wheel(WHEEL_DELTA); step = 12; waits = 0;
    } else if (step == 12 && waits > 2) {
        require(scroll().nPos < bottom(scroll()), "Keypad Enter fixture must be viewing history");
        enter(true); step = 13; waits = 0;
    } else if (step == 13 && waits > 2) {
        require(scroll().nPos == bottom(scroll()), "Keypad Enter must return to live output");
        request(L"scroll-alternate.request", 7);
    } else if (step == 7 && std::filesystem::exists(file(L"scroll-alternate.ready")) && waits > 3) {
        wheel(WHEEL_DELTA, true); step = 8; waits = 0;
    } else if (step == 8 && waits > 2) {
        auto info = scroll(); heldPosition = info.nPos;
        require(heldPosition < bottom(info), "Shift-wheel must reach terminal history from the alternate screen");
        request(L"scroll-redraw.request", 9);
    } else if (step == 9 && std::filesystem::exists(file(L"scroll-redraw.ready")) && waits > 3) {
        auto info = scroll();
        require(info.nPos == (followOutput ? bottom(info) : heldPosition), "TUI cursor movement and repaint must respect the chosen scroll policy");
        captureTestWindow(app.hwnd, L"ui-scroll-alternate.bmp");
        wheel(WHEEL_DELTA, true); step = 14; waits = 0;
    } else if (step == 14 && waits > 2) {
        require(scroll().nPos < bottom(scroll()), "Alternate-screen Enter fixture must be viewing history");
        enter(false); step = 15; waits = 0;
    } else if (step == 15 && waits > 2) {
        require(scroll().nPos == bottom(scroll()), "Enter must return to the live alternate screen");
        wheel(localWheel ? WHEEL_DELTA : 60); step = localWheel ? 21 : 16; waits = 0;
    } else if (step == 21 && waits > 3) {
        require(scroll().nPos < bottom(scroll()), "Without permitted app reporting the alternate-screen wheel must remain local");
        SendMessageW(terminal(), WM_VSCROLL, SB_BOTTOM, 0);
        request(L"scroll-normal.request", 19);
    } else if (step == 16 && waits > 2) {
        require(!std::filesystem::exists(file(L"scroll-app-up.ready")), "Partial wheel must not reach the application yet");
        wheel(60); step = 17; waits = 0;
    } else if (step == 17 && std::filesystem::exists(file(L"scroll-app-up.ready")) && waits > 3) {
        require(scroll().nPos == bottom(scroll()), "Application wheel must not move terminal scrollback");
        captureTestWindow(app.hwnd, L"ui-scroll-app-history.bmp");
        auto cell=firstCell;
        auto point=MAKELPARAM(cell.left+(cell.right-cell.left)*4+(cell.right-cell.left)/2,cell.top+(cell.bottom-cell.top)/2);
        SendMessageW(terminal(),WM_LBUTTONDOWN,MK_LBUTTON,point);
        SendMessageW(terminal(),WM_LBUTTONUP,0,point);
        step=22; waits=0;
    } else if (step == 22 && std::filesystem::exists(file(L"scroll-app-bottom.ready")) && waits > 3) {
        require(scroll().nPos == bottom(scroll()), "Clicking Back to bottom must stay in the application");
        captureTestWindow(app.hwnd,L"ui-scroll-app-bottom.bmp");
        wheel(-WHEEL_DELTA); step = 18; waits = 0;
    } else if (step == 18 && std::filesystem::exists(file(L"scroll-app-down.ready")) && waits > 3) {
        require(scroll().nPos == bottom(scroll()), "Application wheel down must stay on the live screen");
        request(L"scroll-normal.request", 19);
    } else if (step == 19 && std::filesystem::exists(file(L"scroll-normal.ready")) && waits > 3) {
        wheel(WHEEL_DELTA); step = 20; waits = 0;
    } else if (step == 20 && waits > 3) {
        require(scroll().nPos < bottom(scroll()), "Returning to normal screen must restore local wheel despite stale mouse mode");
        app.closeTab(0, true); return true;
    }
    return false;
}
