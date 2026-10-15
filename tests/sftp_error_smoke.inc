// Independent loopback server rejects OPEN and READ with SSH_FX_PERMISSION_DENIED.
// Timer-driven UI checks must complete without blocking or reopening the tab.
template<typename Check> bool testSftpErrors(App &app, Check require) {
    static int step = 0;
    static HWND originalView{};
    auto directory = std::filesystem::path(app.directory) / L"downloads";
    if (step == 0) {
        std::filesystem::create_directory(directory);
        wchar_t port[32]{}, pin[200]{};
        require(GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32) &&
                GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", pin, 200), "Missing SFTP error fixture");
        Profile p; p.name = L"SFTP permission errors"; p.host = L"127.0.0.1"; p.port = _wtoi(port); p.user = L"none";
        wook::saveProfile(p);
        auto path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
        require(store != nullptr, "Cannot open SFTP error profile");
        wsSet(store, "SSHManualHostKeys", wook::utf8(pin).c_str());
        int saved = wsSave(store); wsClose(store); require(saved != 0, "Cannot save SFTP error profile");
        app.connect(p, true, false, false, true); ++step; return false;
    }
    if (app.tabs.empty() || !IsWindow(app.tabs[0]->terminal)) return false;
    auto view = app.tabs[0]->terminal;
    auto state = SendMessageW(view, WM_APP + 121, 0, 0);
    if (!state) return false;
    require(IsWindowEnabled(app.hwnd) && IsWindowEnabled(view), "SFTP errors must leave the window interactive");
    auto download = [&](const wchar_t *name) {
        auto list = GetDlgItem(view, 15); auto text = std::wstring(L"   ") + name;
        LVFINDINFOW find{}; find.flags = LVFI_STRING; find.psz = text.c_str();
        int row = ListView_FindItem(list, -1, &find); require(row >= 0, "Missing SFTP error fixture file");
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        require(IsWindowEnabled(GetDlgItem(view, 2)), "Download must be enabled after a recoverable error");
        SendMessageW(view, WM_COMMAND, 2, 0);
        require(SendMessageW(view, WM_APP + 121, 0, 0) == 0, "Download must start asynchronously");
    };
    auto clean = [&] {
        for (auto &item : std::filesystem::directory_iterator(directory))
            require(!item.path().filename().wstring().starts_with(L".wshell-"), "Failed download must remove its staging file");
        require(!std::filesystem::exists(directory / L"denied-open.bin") &&
                !std::filesystem::exists(directory / L"denied-read.bin"), "Failed download must not publish a destination");
    };
    if (step == 1) {
        require(state == 1, "SFTP fixture must connect"); originalView = view;
        SetWindowTextW(GetDlgItem(view, 10), directory.c_str()); SendMessageW(view, WM_COMMAND, 9, 0);
        download(L"denied-open.bin"); ++step;
    } else if (step == 2) {
        require(state == 1, "Permission denied on OPEN must keep the session connected");
        require(IsWindowEnabled(GetDlgItem(view, 6)) && IsWindowEnabled(GetDlgItem(view, 14)),
                "Permission denied must keep refresh and remote navigation enabled");
        clean(); captureTestWindow(app.hwnd, L"ui-sftp-permission-denied.bmp");
        download(L"allowed.txt"); ++step;
    } else if (step == 3) {
        require(state == 1 && std::filesystem::exists(directory / L"allowed.txt"), "Another download must succeed after denied OPEN");
        download(L"denied-read.bin"); ++step;
    } else if (step == 4) {
        require(state == -1, "Permission denied during a read pipeline must disconnect safely");
        clean(); require(IsWindowEnabled(GetDlgItem(view, 6)), "Disconnected SFTP must offer Refresh to reconnect");
        require(!IsWindowEnabled(GetDlgItem(view, 2)), "Broken session must block transfers until reconnected");
        captureTestWindow(app.hwnd, L"ui-sftp-reconnect.bmp");
        auto list = GetDlgItem(view, 15); SetFocus(list);
        SendMessageW(list, WM_KEYDOWN, VK_F5, 1); SendMessageW(list, WM_KEYUP, VK_F5, 0xc0000001);
        require(SendMessageW(view, WM_APP + 121, 0, 0) == 0, "F5 must start reconnecting"); ++step;
    } else if (step == 5) {
        require(view == originalView && state == 1, "F5 must recover the same SFTP tab");
        download(L"after-reconnect.txt"); ++step;
    } else {
        require(state == 1 && std::filesystem::exists(directory / L"after-reconnect.txt"), "Download must work after reconnect");
        clean(); app.closeTab(0, true); return true;
    }
    return false;
}
