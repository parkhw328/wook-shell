// Exercise the actual workspace layout and lifecycle paths while iconic.
// Child controls stand in for terminals here; the SSH fixture below checks PTY sizes.
template<typename Check> void testMinimizedLayout(App &app, Check require) {
    auto rectangle = [&](HWND window) {
        RECT r{}; GetWindowRect(window, &r); MapWindowPoints(nullptr, app.hwnd, (POINT *)&r, 2); return r;
    };
    for (int i = 0; i < 4; ++i) {
        auto tab = std::make_unique<Tab>(); tab->preview = true;
        tab->terminal = CreateWindowExW(0, L"STATIC", L"Terminal size probe", WS_CHILD,
            0, 0, 600, 400, app.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(tab->terminal != nullptr, "Create hosted terminal geometry probe");
        app.tabs.push_back(std::move(tab));
    }
    app.active = 0;
    struct Scenario { int count; bool maximized, zoomed; };
    for (auto scenario : {Scenario{1, false, false}, {2, false, false}, {3, false, false},
            {4, false, false}, {1, true, false}, {4, true, false}, {4, false, true}}) {
        int count = scenario.count;
        ShowWindow(app.hwnd, scenario.maximized ? SW_MAXIMIZE : SW_RESTORE);
        app.setSplit(count);
        if (scenario.zoomed) app.zoomPane();
        if (count > 1) {
            SendMessageW(app.control(SyncInput), BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(app.control(SendAll), BM_SETCHECK, BST_CHECKED, 0);
            SetWindowTextW(app.control(CommandText), L"keep draft while minimized");
        }
        auto panes = app.panes;
        int width = app.width, height = app.height, sidebar = app.sidebar;
        std::vector<RECT> before;
        for (auto &tab : app.tabs) before.push_back(rectangle(tab->terminal));
        ShowWindow(app.hwnd, SW_MINIMIZE);
        require(IsIconic(app.hwnd), "Fixture must really minimize the workspace");
        for (int tick = 0; tick < 3; ++tick) { app.poll(); app.layout(); }
        require(app.width == width && app.height == height && app.sidebar == sidebar,
            "Minimized relayout must retain the last usable workspace size");
        for (size_t i = 0; i < app.tabs.size(); ++i) {
            auto after = rectangle(app.tabs[i]->terminal);
            require(EqualRect(&before[i], &after), "Minimization must not shrink visible or hidden terminal windows");
        }
        require(app.active == 0 && app.panes == panes && app.splitCount == count,
            "Minimization must preserve selected tab and split identities");
        require(app.paneZoom == scenario.zoomed, "Minimization must preserve pane zoom");
        if (count > 1 && !scenario.zoomed) require(SendMessageW(app.control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SendMessageW(app.control(SendAll), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            ui::value(app.control(CommandText)) == L"keep draft while minimized",
            "Minimization must preserve broadcast options and command draft");
        ShowWindow(app.hwnd, SW_RESTORE);
        for (size_t i = 0; i < app.tabs.size(); ++i) {
            auto after = rectangle(app.tabs[i]->terminal);
            require(EqualRect(&before[i], &after), "Restore must recover the original terminal geometry");
        }
    }
    if (app.paneZoom) app.zoomPane();
    // Poll must still process an exited tab while minimized, including removal
    // of its pane pointer. Skipping all of layout would leave dangling panes.
    auto *last = app.tabs.back().get();
    last->preview = false; last->closing = true;
    last->process = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    require(last->process != nullptr, "Create signaled process-exit stand-in");
    DestroyWindow(last->terminal); last->terminal = nullptr;
    ShowWindow(app.hwnd, SW_MINIMIZE); app.poll();
    require(app.tabs.size() == 3 && std::find(app.panes.begin(), app.panes.end(), last) == app.panes.end(),
        "Background tab exit must remove stale panes even while minimized");
    ShowWindow(app.hwnd, SW_RESTORE);
    RECT window{}; GetWindowRect(app.hwnd, &window);
    auto before = rectangle(app.tabs[0]->terminal);
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left + 100, window.bottom - window.top + 60,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    auto after = rectangle(app.tabs[0]->terminal);
    require(after.right - after.left > before.right - before.left && after.bottom - after.top > before.bottom - before.top,
        "Real resizing after restore must still update terminal dimensions");
    for (auto &tab : app.tabs) DestroyWindow(tab->terminal);
    app.tabs.clear(); app.active = -1; app.splitCount = 1; app.layout();
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left, window.bottom - window.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void runMinimizeSmoke(App &app) {
    static int phase = 0, waits = 0, checks = 0;
    static RECT before{}, window{};
    auto require = [&](bool value, const char *message) { if (!value) throw std::runtime_error(message); ++checks; };
    auto file = [&](const wchar_t *name) { return std::filesystem::path(app.directory) / name; };
    auto received = [&](const wchar_t *name) { return std::filesystem::exists(file(name)); };
    auto send = [&](const wchar_t *text) {
        for (auto ch : std::wstring(text)) SendMessageW(app.tabs[0]->terminal, WM_CHAR, ch, 1);
        SendMessageW(app.tabs[0]->terminal, WM_CHAR, L'\r', 1);
    };
    auto finish = [&] {
        phase = 99; KillTimer(app.hwnd, 1);
        for (auto &tab : app.tabs) if (tab->process) PostMessageW(tab->terminal, WM_CLOSE, 0, 0);
        DestroyWindow(app.hwnd);
    };
    try {
        if (++waits > 120) throw std::runtime_error("Minimize regression timed out at phase " + std::to_string(phase));
        if (phase == 0) {
            // Prevent nested timer dispatch while the synchronous layout fixture runs.
            phase = -1; KillTimer(app.hwnd, 1);
            if (!GetEnvironmentVariableW(L"WOOK_TEST_MINIMIZE_SSH_ONLY", nullptr, 0)) testMinimizedLayout(app, require);
            if (!GetEnvironmentVariableW(L"WOOK_TEST_PORT", nullptr, 0)) {
                std::ofstream(file(L"minimize-result.txt")) << "PASS " << checks << " checks\n";
                finish(); return;
            }
            wchar_t port[32]{}, fingerprint[200]{};
            GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32);
            require(GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", fingerprint, 200), "Missing SSH host key pin");
            Profile p; p.name = L"Minimize regression"; p.host = L"127.0.0.1"; p.user = L"minimize"; p.port = _wtoi(port);
            wook::saveProfile(p);
            auto path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
            require(store != nullptr, "Create isolated SSH session");
            wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
            require(wsSave(store), "Pin loopback SSH host key"); wsClose(store);
            app.connect(p, true); phase = 1; waits = 0; SetTimer(app.hwnd, 1, 350, nullptr);
        } else if (phase == 1 && app.inputReady(app.tabs[0].get()) && waits > 8) {
            GetClientRect(app.tabs[0]->terminal, &before); GetWindowRect(app.hwnd, &window);
            captureTestWindow(app.tabs[0]->terminal, L"ui-minimize-terminal-before.bmp");
            send(L"baseline"); phase = 2; waits = 0;
        } else if (phase == 2 && received(L"baseline.ready")) {
            ShowWindow(app.hwnd, SW_MINIMIZE);
            require(IsIconic(app.hwnd), "SSH workspace must really be minimized");
            // Same poll path as a hidden terminal process exiting after a long idle.
            auto exited = std::make_unique<Tab>(); exited->closing = true;
            exited->process = CreateEventW(nullptr, TRUE, TRUE, nullptr);
            require(exited->process != nullptr, "Create background exit stand-in");
            app.tabs.push_back(std::move(exited)); app.poll();
            require(app.tabs.size() == 1, "Background exit must finish while minimized");
            // Late terminal attachment is another layout entry point outside WM_SIZE.
            SendMessageW(app.hwnd, WM_APP + 42, (WPARAM)app.tabs[0]->terminal, app.tabs[0]->pid);
            phase = 3; waits = 0;
        } else if (phase == 3 && waits > 8) {
            app.layout(); send(L"minimized"); phase = 4; waits = 0;
        } else if (phase == 4 && received(L"minimized.ready") && waits > 4) {
            RECT current{}; GetClientRect(app.tabs[0]->terminal, &current);
            require(EqualRect(&before, &current), "Live SSH terminal must retain its client size while minimized");
            ShowWindow(app.hwnd, SW_RESTORE); phase = 5; waits = 0;
        } else if (phase == 5 && waits > 4) {
            RECT current{}; GetClientRect(app.tabs[0]->terminal, &current);
            require(EqualRect(&before, &current) && app.inputReady(app.tabs[0].get()),
                "Restore must preserve terminal geometry and the live SSH connection");
            captureTestWindow(app.hwnd, L"ui-minimize-restored.bmp", true);
            captureTestWindow(app.tabs[0]->terminal, L"ui-minimize-terminal-restored.bmp");
            send(L"restored"); phase = 6; waits = 0;
        } else if (phase == 6 && received(L"restored.ready")) {
            SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left + 100, window.bottom - window.top + 60,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            phase = 7; waits = 0;
        } else if (phase == 7 && waits > 4) {
            send(L"resized"); phase = 8; waits = 0;
        } else if (phase == 8 && received(L"resized.ready")) {
            std::ofstream(file(L"minimize-result.txt")) << "PASS " << checks << " checks\n";
            finish();
        }
    } catch (const std::exception &error) {
        std::ofstream(file(L"minimize-result.txt")) << "FAIL " << error.what() << '\n';
        finish();
    }
}
