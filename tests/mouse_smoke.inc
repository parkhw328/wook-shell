// Win32 mouse-message, clipboard and real SSH regression. Native desktop focus
// routing is covered separately by focus-integration.cjs.
static HGLOBAL mouseClipboardBackup = nullptr;
static bool mouseClipboardChanged = false;
static void restoreMouseTestClipboard(HWND owner) {
    if (!mouseClipboardChanged || !OpenClipboard(owner)) return;
    EmptyClipboard();
    if (mouseClipboardBackup && SetClipboardData(CF_UNICODETEXT, mouseClipboardBackup)) mouseClipboardBackup = nullptr;
    CloseClipboard();
    if (mouseClipboardBackup) GlobalFree(mouseClipboardBackup);
    mouseClipboardBackup = nullptr; mouseClipboardChanged = false;
}
template<typename Check> bool testTerminalMouse(App &app, Check require) {
    static int step = 0, waits = 0;
    static const bool applicationMouse = GetEnvironmentVariableW(L"WOOK_TEST_APP_MOUSE", nullptr, 0) != 0;
    static RECT cell{};
    auto file = [&](const wchar_t *name) { return std::filesystem::path(app.directory) / name; };
    auto clipboard = [&] {
        require(OpenClipboard(app.hwnd), "Cannot read the clipboard for mouse regression");
        std::wstring text;
        if (auto data = GetClipboardData(CF_UNICODETEXT)) {
            if (auto buffer = static_cast<const wchar_t *>(GlobalLock(data))) {
                text = buffer; GlobalUnlock(data);
            }
        }
        CloseClipboard(); return text;
    };
    auto setClipboard = [&](const std::wstring &text) {
        require(OpenClipboard(app.hwnd), "Cannot set the clipboard for mouse regression");
        EmptyClipboard(); mouseClipboardChanged = true;
        auto data = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        auto buffer = static_cast<wchar_t *>(GlobalLock(data));
        if (!buffer) { CloseClipboard(); throw std::runtime_error("Cannot allocate test clipboard"); }
        memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(wchar_t)); GlobalUnlock(data);
        auto result = SetClipboardData(CF_UNICODETEXT, data); CloseClipboard();
        if (!result) GlobalFree(data);
        require(result != nullptr, "Cannot write test clipboard");
    };
    auto terminal = [&] { return app.tabs[0]->terminal; };
    auto focus = [&] {
        GUITHREADINFO info{sizeof(info)};
        require(GetGUIThreadInfo(GetWindowThreadProcessId(terminal(), nullptr), &info), "Cannot query terminal focus");
        return info;
    };
    auto point = [&](int column) {
        return MAKELPARAM(cell.left + (cell.right - cell.left) * column + (cell.right - cell.left) / 2,
                         cell.top + (cell.bottom - cell.top) / 2);
    };
    auto drag = [&](bool shift) {
        SetFocus(app.control(Search));
        auto modifiers = shift ? MK_SHIFT : 0;
        SendMessageW(terminal(), WM_LBUTTONDOWN, MK_LBUTTON | modifiers, point(0));
        require(focus().hwndFocus == terminal(), "Drag must focus the clicked single terminal");
        SendMessageW(terminal(), WM_MOUSEMOVE, MK_LBUTTON | modifiers, point(9));
        SendMessageW(terminal(), WM_LBUTTONUP, modifiers, point(9));
    };
    auto paste = [&](bool shift) {
        SetFocus(app.control(Search));
        SendMessageW(terminal(), WM_RBUTTONDOWN, shift ? MK_SHIFT : 0, point(12));
        require(focus().hwndFocus == terminal(), "Right-click must focus the clicked single terminal");
        SendMessageW(terminal(), WM_RBUTTONUP, shift ? MK_SHIFT : 0, point(12));
    };
    { std::ofstream progress(file(L"mouse-progress.json")); progress << "{\"step\":" << step << "}\n"; }
    if (++waits > 40) throw std::runtime_error("Mouse regression timed out waiting for SSH input");
    if (step == 0) {
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            auto text = clipboard();
            auto bytes = (text.size() + 1) * sizeof(wchar_t);
            mouseClipboardBackup = GlobalAlloc(GMEM_MOVEABLE, bytes);
            auto buffer = GlobalLock(mouseClipboardBackup);
            require(buffer != nullptr, "Cannot preserve clipboard text");
            memcpy(buffer, text.c_str(), bytes); GlobalUnlock(mouseClipboardBackup);
        }
        wchar_t port[32]{}, fingerprint[200]{};
        require(GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32), "Missing mouse SSH port");
        require(GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", fingerprint, 200), "Missing mouse SSH host key");
        Profile p; p.name = L"Mouse regression"; p.user = L"mouse"; p.host = L"127.0.0.1"; p.port = _wtoi(port);
        wook::saveProfile(p);
        auto path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
        require(store != nullptr, "Cannot create mouse session");
        wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
        wsSet(store, "NoMouseReporting", "0"); // A legacy saved session, before the new preference existed.
        if (applicationMouse) wsSet(store, "WookMouseClipboard", "0");
        require(wsSave(store), "Cannot pin mouse SSH host key"); wsClose(store);
        app.connect(p, true); ++step; waits = 0;
    } else if (step == 1 && IsWindow(terminal()) && std::filesystem::exists(file(L"mouse-screen.ready")) && waits > 3) {
        app.focusActive(); cell = focus().rcCaret;
        require(focus().hwndCaret == terminal(), "Mouse geometry must belong to the test terminal");
        require(cell.right > cell.left && cell.bottom > cell.top, "Terminal must expose its native character-cell geometry");
        captureTestWindow(app.hwnd, L"ui-mouse-terminal.bmp");
        setClipboard(L"before-selection"); drag(false); ++step; waits = 0;
    } else if (step == 2) {
        require(clipboard() == L"COPY-MOUSE", "Normal drag must auto-copy exact selected text");
        setClipboard(L"normal-paste-한글"); paste(false); ++step; waits = 0;
    } else if (step == 3 && std::filesystem::exists(file(L"mouse-normal-paste.ready"))) {
        std::ofstream(file(L"mouse-enable.request")) << "enable\n"; ++step; waits = 0;
    } else if (step == 4 && std::filesystem::exists(file(L"mouse-reporting.ready"))) {
        setClipboard(L"raw-selection-sentinel"); drag(false); ++step; waits = 0;
    } else if (step == 5) {
        require(clipboard() == (applicationMouse ? L"raw-selection-sentinel" : L"COPY-MOUSE"),
                "Mouse policy must choose between application input and local drag-copy");
        setClipboard(applicationMouse ? L"raw-paste-sentinel" : L"preferred-paste-한글"); paste(false); ++step; waits = 0;
    } else if (step == 6 && std::filesystem::exists(file(applicationMouse ? L"mouse-raw-clicks.ready" : L"mouse-preferred-paste.ready"))) {
        drag(true); ++step; waits = 0;
    } else if (step == 7) {
        require(clipboard() == L"COPY-MOUSE", "Shift-drag must auto-copy while application mouse reporting is active");
        setClipboard(L"shift-paste-한글"); paste(true); ++step; waits = 0;
    } else if (step == 8 && std::filesystem::exists(file(L"mouse-shift-paste.ready"))) {
        std::ofstream(file(L"mouse-toggle.request")) << "toggle\n";
        app.action(SessionSettings); step = 12; waits = 0;
    } else if (step == 12 && std::filesystem::exists(file(L"mouse-toggled.ready")) &&
               std::filesystem::exists(file(L"settings-live-1.json")) && waits > 3) {
        setClipboard(L"toggled-selection-sentinel"); drag(false); ++step; waits = 0;
    } else if (step == 13) {
        require(clipboard() == (applicationMouse ? L"COPY-MOUSE" : L"toggled-selection-sentinel"),
                "Applying mouse preference must change drag handling in the connected terminal");
        setClipboard(applicationMouse ? L"toggled-paste-한글" : L"toggled-paste-sentinel"); paste(false); ++step; waits = 0;
    } else if (step == 14 && std::filesystem::exists(file(applicationMouse ? L"mouse-toggled-paste.ready" : L"mouse-toggled-raw.ready"))) {
        std::ofstream(file(L"mouse-disable.request")) << "disable\n"; step = 9; waits = 0;
    } else if (step == 9 && std::filesystem::exists(file(L"mouse-restored.ready"))) {
        setClipboard(L"restored-selection-sentinel"); drag(false); ++step; waits = 0;
    } else if (step == 10) {
        require(clipboard() == L"COPY-MOUSE", "Normal drag must return after the application releases mouse capture");
        setClipboard(L"restored-paste-한글"); paste(false); ++step; waits = 0;
    } else if (step == 11 && std::filesystem::exists(file(L"mouse-restored-paste.ready"))) {
        restoreMouseTestClipboard(app.hwnd);
        app.closeTab(0, true); return true;
    }
    return false;
}
