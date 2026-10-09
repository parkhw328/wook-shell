// Observe the real terminal scrollbar while loopback SSH output continues.
// Wheel messages exercise the engine, not native OS pointer routing.
template<typename Check> bool testTerminalScroll(App &app, Check require) {
    static int step = 0, waits = 0, heldPosition = 0, previousBottom = 0;
    static const bool followOutput = GetEnvironmentVariableW(L"WOOK_TEST_FOLLOW_OUTPUT", nullptr, 0) != 0;
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
    auto wheel = [&](int delta) {
        RECT area{}; GetClientRect(terminal(), &area);
        POINT point{area.right / 2, area.bottom / 2}; ClientToScreen(terminal(), &point);
        SendMessageW(terminal(), WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(point.x, point.y));
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
        wsSet(store, "ScrollBar", "1"); // Observe position via Win32 instead of a test-only engine hook.
        wsSet(store, "ScrollOnDisp", "1"); // Older saved sessions include the old engine default.
        if (followOutput) wsSet(store, "WookScrollOnDisp", "1");
        require(wsSave(store), "Cannot save scroll fixture"); wsClose(store);
        app.connect(p, true); step = 1; waits = 0;
    } else if (step == 1 && IsWindow(terminal()) && std::filesystem::exists(file(L"scroll-screen.ready")) && waits > 3) {
        app.focusActive();
        auto info = scroll(); previousBottom = bottom(info);
        captureTestWindow(app.hwnd, L"ui-scroll-initial.bmp");
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
        request(L"scroll-alternate.request", 7);
    } else if (step == 7 && std::filesystem::exists(file(L"scroll-alternate.ready")) && waits > 3) {
        wheel(WHEEL_DELTA); step = 8; waits = 0;
    } else if (step == 8 && waits > 2) {
        auto info = scroll(); heldPosition = info.nPos;
        require(heldPosition < bottom(info), "Wheel must also reach history from the alternate screen");
        request(L"scroll-redraw.request", 9);
    } else if (step == 9 && std::filesystem::exists(file(L"scroll-redraw.ready")) && waits > 3) {
        auto info = scroll();
        require(info.nPos == (followOutput ? bottom(info) : heldPosition), "TUI cursor movement and repaint must respect the chosen scroll policy");
        captureTestWindow(app.hwnd, L"ui-scroll-alternate.bmp");
        app.closeTab(0, true); return true;
    }
    return false;
}
