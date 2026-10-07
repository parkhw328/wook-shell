// Compiled only into ui-smoke-tests.exe, never into the release application.
template<typename Check> void testNativeKeys(const std::wstring &directory, Check require) {
    using Key = std::unique_ptr<WsKey, decltype(&wsKeyFree)>;
    auto publicText = [](WsKey *key) {
        char *text = wsKeyPublic(key); std::string result = text ? text : "";
        wsKeyStringFree(text); return result;
    };
    for (int bits : {0, 3072, 4096}) {
        Key key(wsKeyGenerate(bits), wsKeyFree);
        require(key != nullptr, "Native key generation failed");
        auto original = publicText(key.get());
        require(original.starts_with(bits ? "ssh-rsa " : "ssh-ed25519 "), "Wrong generated key algorithm");
        auto path = directory + L"\\generated-키-" + std::to_wstring(bits) + L".ppk";
        require(wsKeySave(key.get(), path.c_str(), "test-only-passphrase") != 0, "Encrypted private key save failed");
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        std::string serialized((std::istreambuf_iterator<char>(input)), {}); input.close();
        require(serialized.starts_with("PuTTY-User-Key-File-3:") && serialized.find("Encryption: aes256-cbc") != std::string::npos,
                "Private key must use encrypted PPK v3");
        Key wrong(wsKeyLoad(path.c_str(), "wrong-passphrase"), wsKeyFree);
        require(wrong == nullptr, "Wrong private-key passphrase must be rejected");
        Key restored(wsKeyLoad(path.c_str(), "test-only-passphrase"), wsKeyFree);
        require(restored && publicText(restored.get()) == original, "Private key save/load round trip changed the key");
        if (bits == 3072) {
            require(wsKeySave(restored.get(), (directory + L"\\client-key.ppk").c_str(), "") != 0, "Cannot write ephemeral SSH test key");
            std::ofstream output(std::filesystem::path(directory) / L"client-key.pub"); output << original << '\n';
            output.close(); require(bool(output), "Cannot write ephemeral public key");
        }
    }
    wchar_t fixture[32768]{};
    require(GetEnvironmentVariableW(L"WOOK_TEST_IMPORT_KEY", fixture, 32768) > 0, "Missing independent OpenSSH import fixture");
    Key imported(wsKeyLoad(fixture, "fixture-passphrase"), wsKeyFree);
    if (!imported) throw std::runtime_error(std::string("OpenSSH import: ") + wsKeyError());
    require(imported && publicText(imported.get()).starts_with("ssh-ed25519 "), "Encrypted OpenSSH import failed");
    auto converted = directory + L"\\imported.ppk";
    require(wsKeySave(imported.get(), converted.c_str(), "converted-passphrase") != 0, "Imported key conversion failed");
    Key reopened(wsKeyLoad(converted.c_str(), "converted-passphrase"), wsKeyFree);
    require(reopened && publicText(reopened.get()) == publicText(imported.get()), "Converted OpenSSH key changed");
}
void runUiSmoke(App &app) {
    static int phase = 0;
    static int waits = 0;
    static int checks = 0;
    auto require = [&](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
        ++checks;
    };
    auto ready = [&] {
        for (auto &t : app.tabs) if (!IsWindow(t->terminal)) return false;
        return true;
    };
    auto passwordConnection = [&](bool fallback) {
        wchar_t port[32]{}, fingerprint[200]{};
        GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32);
        GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", fingerprint, 200);
        Profile p; p.name = fallback ? L"Password fallback" : L"Password SSH test";
        p.host = L"127.0.0.1"; p.user = fallback ? L"password-fallback" : L"password"; p.port = _wtoi(port);
        if (fallback) wook::saveProfile(p, L"", wook::PasswordAction::replace, L"deliberately-wrong-test-value");
        else { require(editHost(app.hwnd, p, false), "Password host editor must save"); require(p.passwordSaved, "Password save option must persist"); }
        wchar_t *path = wsPath(L"sessions", wook::utf8(p.name).c_str()); auto store = wsOpen(path, 1); free(path);
        wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
        require(wsSave(store), "Cannot pin password fixture host key"); wsClose(store);
        if (!fallback) {
            char *decoded = nullptr;
            require(wsLoadSavedPassword(wook::utf8(p.name).c_str(), "127.0.0.1", p.port, "password", &decoded) == 1, "Saved UI password must decrypt");
            require(std::string(decoded) == wook::utf8(testPassword()), "UI password must preserve the exact input"); wsPasswordFree(decoded);
        }
        app.refresh(); app.connect(p, true, false, false);
    };
    auto closeTestSession = [&](int index) { app.tabs[index]->closing = true; app.closeTab(index); };
    auto received = [&](const wchar_t *marker) { return std::filesystem::exists(std::filesystem::path(app.directory) / marker); };
    if (phase < 0) return;
    { std::ofstream progress(std::filesystem::path(app.directory) / L"ui-smoke-progress.json");
      progress << "{\"phase\":" << phase << ",\"waits\":" << waits << ",\"tabs\":" << app.tabs.size() << "}\n"; }
    try {
        if (++waits > 80) throw std::runtime_error("Timed out waiting for hosted terminal windows.");
        if (phase == 0) {
            require(GetClassLongPtrW(app.hwnd, GCLP_HICON) != 0 && GetClassLongPtrW(app.hwnd, GCLP_HICONSM) != 0, "Executable must include large and small app icons");
            auto fontDc = GetDC(app.hwnd); auto oldFont = SelectObject(fontDc, ui::font()); wchar_t face[100]{};
            GetTextFaceW(fontDc, 100, face); SelectObject(fontDc, oldFont); ReleaseDC(app.hwnd, fontDc);
            require(std::wstring(face) == L"JetBrains Mono", "Embedded UI font must resolve without installed fonts");
            captureTestWindow(app.hwnd, L"ui-home.bmp");
            phase = -1;
            Profile p; p.name = L"서울 test host"; p.host = L"127.0.0.1"; p.user = L"developer"; p.group = L"Local";
            require(editHost(app.hwnd, p, false), "Host editor did not save");
            app.refresh(); require(app.profiles.size() == 1, "Saved host is missing from the sidebar");
            showKeyManager(app.hwnd);
            require(std::filesystem::exists(std::filesystem::path(app.directory) / L"ui-key-manager.bmp"), "Native key manager must generate and display a key");
            testNativeKeys(app.directory, require);
            app.action(Preview); phase = 1; waits = 0;
        } else if (phase == 1 && ready()) {
            require(app.tabs.size() == 1, "Preview must open one tab");
            wchar_t childImage[32768]{}; DWORD length = 32768;
            require(QueryFullProcessImageNameW(app.tabs[0]->process, 0, childImage, &length) &&
                std::filesystem::path(childImage).filename() == L"wShell.exe", "Tabs must run the same executable, without a PuTTY helper");
            require(GetParent(app.tabs[0]->terminal) == app.hwnd, "Terminal is not parented to the workspace");
            require(GetWindowLongPtrW(app.tabs[0]->terminal, GWL_STYLE) & WS_CHILD, "Terminal must have child-window style");
            require(!(GetWindowLongPtrW(app.tabs[0]->terminal, GWL_STYLE) & WS_CAPTION), "Embedded terminals must not have a second title bar");
            phase = 2; waits = 0;
        } else if (phase == 2 && waits > 2) {
            captureTestWindow(app.hwnd, L"ui-terminal.bmp");
            app.command(2); phase = 3; waits = 0;
        } else if (phase == 3 && ready()) {
            require(app.tabs.size() == 2 && app.tabs[0]->pid != app.tabs[1]->pid, "Duplicated tabs need independent processes");
            require(!IsWindowVisible(app.tabs[0]->terminal) && IsWindowVisible(app.tabs[1]->terminal), "Only the selected terminal should be visible");
            app.select(0);
            require(IsWindowVisible(app.tabs[0]->terminal) && !IsWindowVisible(app.tabs[1]->terminal), "Switching tabs must preserve and hide terminals");
            RECT r; GetClientRect(app.tabs[0]->terminal, &r);
            require(r.right > 300 && r.bottom > 200, "Terminal was not resized into the content area");
            app.command(8); app.command(8);
            require(!app.fullscreen, "Full screen did not restore");
            std::swap(app.tabs[0], app.tabs[1]); app.select(0);
            captureTestWindow(app.hwnd, L"ui-tabs.bmp");
            app.closeTab(1); phase = 4; waits = 0;
        } else if (phase == 4 && app.tabs.size() == 1) {
            app.closeTab(0); phase = 5; waits = 0;
        } else if (phase == 5 && app.tabs.empty()) {
            require(app.active == -1, "Closing the last tab must return to the workspace");
            wchar_t port[32], fingerprint[200];
            require(GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32) > 0, "Missing loopback SSH fixture");
            require(GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", fingerprint, 200) > 0, "Missing host key pin");
            Profile p; p.name = L"한글 SSH 연결"; p.host = L"127.0.0.1"; p.user = L"key"; p.port = _wtoi(port);
            p.keyFile = app.directory + L"\\client-key.ppk";
            require(editHost(app.hwnd, p, false), "Public key authentication host must save");
            wook::saveProfile(p);
            wchar_t *path = wsPath(L"sessions", wook::utf8(p.name).c_str());
            auto store = wsOpen(path, 1); free(path);
            require(store != nullptr, "Cannot configure SSH fixture");
            wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
            require(wsSave(store), "Cannot store host key pin"); wsClose(store);
            app.refresh(); app.connect(p, true, false, true);
            phase = 6; waits = 0;
        } else if (phase == 6 && ready() && waits > 7) {
            wchar_t *path = wsPath(L"sessions", wook::utf8(app.tabs[0]->storageName).c_str());
            auto store = wsOpen(path, 0); free(path);
            require(store && wsGet(store, "PingIntervalSecs") && std::string(wsGet(store, "PingIntervalSecs")) == "45", "Connection settings must persist keepalives");
            require(wsGet(store, "ScrollbackLines") && std::string(wsGet(store, "ScrollbackLines")) == "12000", "Connection settings must persist scrollback");
            wsClose(store);
            captureTestWindow(app.hwnd, L"ui-ssh.bmp");
            SendMessageW(app.tabs[0]->terminal, WM_CHAR, L'x', 1);
            RECT r; GetWindowRect(app.hwnd, &r);
            SetWindowPos(app.hwnd, nullptr, 0,0, r.right-r.left-40, r.bottom-r.top-20, SWP_NOMOVE | SWP_NOZORDER);
            app.action(SessionSettings);
            phase = 7; waits = 0;
        } else if (phase == 7 && std::filesystem::exists(std::filesystem::path(app.directory) / L"settings-live-1.json")) {
            app.action(SessionSettings); phase = 9; waits = 0;
        } else if (phase == 9 && std::filesystem::exists(std::filesystem::path(app.directory) / L"settings-live-2.json")) {
            app.action(SessionSettings); phase = 10; waits = 0;
        } else if (phase == 10 && std::filesystem::exists(std::filesystem::path(app.directory) / L"settings-live-3.json")) {
            PostMessageW(app.tabs[0]->terminal, WM_CLOSE, 0, 0);
            phase = 8; waits = 0;
        } else if (phase == 8 && app.tabs[0]->ended) {
            app.closeTab(0); phase = -1; passwordConnection(false); phase = 11; waits = 0;
        } else if (phase == 11 && ready() && waits > 10 && received(L"password-shell-1.ready")) {
            captureTestWindow(app.hwnd, L"ui-password-ssh.bmp");
            SendMessageW(app.tabs[0]->terminal, WM_CHAR, L'y', 1);
            app.command(2); phase = 12; waits = 0;
        } else if (phase == 12 && ready() && waits > 8 && received(L"password-shell-2.ready") && received(L"password-input-y.ready")) {
            require(app.tabs.size() == 2, "Password session must duplicate");
            PostMessageW(app.tabs[0]->terminal, WM_APP + 60, 0, 0);
            closeTestSession(1); phase = 13; waits = 0;
        } else if (phase == 13 && app.tabs.size() == 1 && ready() && waits > 10 && received(L"password-shell-3.ready")) {
            SendMessageW(app.tabs[0]->terminal, WM_CHAR, L'z', 1);
            phase = 18; waits = 0;
        } else if (phase == 18 && received(L"password-input-z.ready")) {
            closeTestSession(0); phase = 14; waits = 0;
        } else if (phase == 14 && app.tabs.empty()) {
            passwordConnection(true); phase = 15; waits = 0;
        } else if (phase == 15 && ready() && waits > 10 && received(L"password-rejected.ready")) {
            captureTestWindow(app.hwnd, L"ui-password-fallback.bmp");
            auto fixture = testPassword();
            for (wchar_t c : fixture) PostMessageW(app.tabs[0]->terminal, WM_CHAR, c, 1);
            SecureZeroMemory(fixture.data(), fixture.size() * sizeof(wchar_t));
            PostMessageW(app.tabs[0]->terminal, WM_CHAR, L'\r', 1);
            phase = 16; waits = 0;
        } else if (phase == 16 && ready() && waits > 10 && received(L"password-fallback-shell.ready")) {
            captureTestWindow(app.hwnd, L"ui-password-retry.bmp");
            closeTestSession(0); phase = 17; waits = 0;
        } else if (phase == 17 && app.tabs.empty()) {
            app.localShell(); phase = 19; waits = 0;
        } else if (phase == 19 && ready() && waits > 8) {
            require(app.tabs[0]->profile.protocol == L"local", "CMD must use the local terminal backend");
            auto command = L"echo wshell-cmd-ok>\"" + app.directory + L"\\local-cmd.ready\"\r";
            for (auto c : command) PostMessageW(app.tabs[0]->terminal, WM_CHAR, c, 1);
            phase = 20; waits = 0;
        } else if (phase == 20 && received(L"local-cmd.ready")) {
            captureTestWindow(app.hwnd, L"ui-local-cmd.bmp");
            app.command(2); phase = 21; waits = 0;
        } else if (phase == 21 && ready() && waits > 6) {
            require(app.tabs.size() == 2 && app.tabs[0]->pid != app.tabs[1]->pid, "Local duplicate must have a separate process");
            auto command = L"echo wshell-duplicate-ok>\"" + app.directory + L"\\local-duplicate.ready\"\r";
            for (auto c : command) PostMessageW(app.tabs[1]->terminal, WM_CHAR, c, 1);
            phase = 22; waits = 0;
        } else if (phase == 22 && received(L"local-duplicate.ready")) {
            closeTestSession(1); closeTestSession(0); phase = 23; waits = 0;
        } else if (phase == 23 && app.tabs.empty()) {
            app.localShell(true); phase = 24; waits = 0;
        } else if (phase == 24 && ready() && waits > 10) {
            auto command = L"[IO.File]::WriteAllText(\"" + app.directory + L"\\local-powershell.ready\",'wshell-powershell-ok')\r";
            for (auto c : command) PostMessageW(app.tabs[0]->terminal, WM_CHAR, c, 1);
            phase = 25; waits = 0;
        } else if (phase == 25 && received(L"local-powershell.ready")) {
            captureTestWindow(app.hwnd, L"ui-local-powershell.bmp");
            for (auto c : std::wstring(L"exit\r")) PostMessageW(app.tabs[0]->terminal, WM_CHAR, c, 1);
            phase = 26; waits = 0;
        } else if (phase == 26 && app.tabs[0]->ended) {
            require(true, "PowerShell exit must finish the tab");
            closeTestSession(0); phase = 27; waits = 0;
        } else if (phase == 27 && app.tabs.empty()) {
            std::ofstream result(std::filesystem::path(app.directory) / L"ui-smoke-result.json");
            result << "{\"passed\":true,\"checks\":" << checks << "}\n";
            result.close(); phase = -1; DestroyWindow(app.hwnd);
        }
    } catch (const std::exception &e) {
        std::ofstream result(std::filesystem::path(app.directory) / L"ui-smoke-result.json");
        result << "{\"passed\":false,\"phase\":" << phase << ",\"error\":\"" << e.what() << "\"}\n";
        result.close(); phase = -1; DestroyWindow(app.hwnd);
    }
}
