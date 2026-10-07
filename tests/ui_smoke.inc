// Compiled only into ui-smoke-tests.exe, never into the release application.
#include "focus_smoke.inc"
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
    static const bool focusOnly = GetEnvironmentVariableW(L"WOOK_TEST_FOCUS_ONLY", nullptr, 0) != 0;
    static int phase = focusOnly ? 33 : 0;
    static int waits = 0;
    static int checks = 0;
    static std::vector<Tab *> zoomGroup;
    auto require = [&](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
        ++checks;
    };
    auto ready = [&] {
        if (app.tabs.empty()) return false;
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
            phase = -1;
            require(editHost(app.hwnd, p, false), "Public key authentication host must save");
            wchar_t *path = wsPath(L"sessions", wook::utf8(p.name).c_str());
            auto store = wsOpen(path, 1); free(path);
            require(store != nullptr, "Cannot configure SSH fixture");
            wsSet(store, "SSHManualHostKeys", wook::utf8(fingerprint).c_str());
            require(wsSave(store), "Cannot store host key pin"); wsClose(store);
            app.refresh(); app.connect(p, true, false, true);
            phase = 6; waits = 0;
        } else if (phase == 6 && ready() && received(L"key-shell.ready") && app.inputReady(app.tabs[0].get()) && waits > 7) {
            wchar_t *path = wsPath(L"sessions", wook::utf8(app.tabs[0]->storageName).c_str());
            auto store = wsOpen(path, 0); free(path);
            require(store && wsGet(store, "PingIntervalSecs") && std::string(wsGet(store, "PingIntervalSecs")) == "45", "Connection settings must persist keepalives");
            require(wsGet(store, "ScrollbackLines") && std::string(wsGet(store, "ScrollbackLines")) == "12000", "Connection settings must persist scrollback");
            wsClose(store);
            captureTestWindow(app.hwnd, L"ui-ssh.bmp");
            GUITHREADINFO caret{sizeof(caret)};
            require(GetGUIThreadInfo(GetWindowThreadProcessId(app.tabs[0]->terminal, nullptr), &caret) &&
                    caret.hwndCaret == app.tabs[0]->terminal, "Hosted terminal must expose its actual IME caret");
            auto ime = wsImeCreate(app.tabs[0]->terminal, [](void *, const wchar_t *, int) {}, mk_wcwidth, nullptr);
            require(ime != nullptr, "Cannot preview native IME layout");
            wsImePosition(ime, caret.rcCaret.left, caret.rcCaret.top,
                caret.rcCaret.right-caret.rcCaret.left, caret.rcCaret.bottom-caret.rcCaret.top,
                ui::font(), RGB(206,205,195), RGB(16,15,15));
            wsImeStart(ime); wsImeApply(ime, nullptr, 0, L"한", 1, 1);
            captureTestWindow(app.hwnd, L"ui-ime-composition.bmp");
            wsImeDestroy(ime);
            SendMessageW(app.tabs[0]->terminal, WM_CHAR, L'x', 1);
            // Exercise the Unicode fallback through the actual embedded window and SSH.
            for (auto c : std::wstring(L"|ime:한글\U0001f642|"))
                SendMessageW(app.tabs[0]->terminal, WM_IME_CHAR, c, 1);
            DeleteFileW((app.directory+L"\\pty-resize.ready").c_str());
            RECT r; GetWindowRect(app.hwnd, &r);
            SetWindowPos(app.hwnd, nullptr, 0,0, r.right-r.left-40, r.bottom-r.top-20, SWP_NOMOVE | SWP_NOZORDER);
            // CI desktops may already constrain the outer window to its minimum size.
            // Resize the hosted terminal itself and wait for the independent server's observation.
            RECT terminalBefore{}, terminalAfter{}; GetClientRect(app.tabs[0]->terminal,&terminalBefore);
            SetWindowPos(app.tabs[0]->terminal,nullptr,0,0,terminalBefore.right-80,terminalBefore.bottom-48,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            GetClientRect(app.tabs[0]->terminal,&terminalAfter);
            require(terminalAfter.right<terminalBefore.right && terminalAfter.bottom<terminalBefore.bottom,"PTY resize test must change the terminal client area");
            phase = 44; waits = 0;
        } else if (phase == 44 && received(L"pty-resize.ready")) {
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
            auto local=std::filesystem::path(app.directory)/L"sftp-local";
            std::filesystem::create_directory(local);std::filesystem::create_directory(std::filesystem::path(app.directory)/L"sftp-download");
            {std::ofstream file(local/L"upload-한글.txt",std::ios::binary);file<<"Native SFTP · 한글 🙂\n";}
            {std::ofstream file(local/L"empty.txt");}
            {std::ofstream file(local/L".hidden.txt");}
            {std::ofstream file(local/L"hidden-attribute.txt");}
            SetFileAttributesW((local/L"hidden-attribute.txt").c_str(),FILE_ATTRIBUTE_HIDDEN);
            auto profiles=wook::loadProfiles();
            auto found=std::find_if(profiles.begin(),profiles.end(),[](auto&p){return p.name==L"한글 SSH 연결";});
            require(found!=profiles.end(),"Missing SFTP key profile");
            app.connect(*found,true,false,false,true);phase=28;waits=0;
        } else if (phase == 28 && ready() && SendMessageW(app.tabs[0]->terminal,WM_APP+121,0,0)==1) {
            require(app.tabs[0]->files && !app.tabs[0]->process,"SFTP must be an in-process tab with an independently supervised transport");
            auto view=app.tabs[0]->terminal;
            SetWindowTextW(GetDlgItem(view,10),(app.directory+L"\\sftp-local").c_str());SendMessageW(view,WM_COMMAND,9,0);
            require(ListView_GetItemCount(GetDlgItem(view,11))==2,"SFTP must list the local fixture");
            require(SendMessageW(GetDlgItem(view,16),BM_GETCHECK,0,0)==BST_UNCHECKED,"Hidden files must be off by default");
            require(ListView_GetItemCount(GetDlgItem(view,15))==2,"Remote dotfiles and dotfolders must be hidden by default");
            SendMessageW(GetDlgItem(view,16),BM_CLICK,0,0);
            require(ListView_GetItemCount(GetDlgItem(view,11))==4,"Show hidden must reveal local dotfiles and Windows hidden attributes");
            require(ListView_GetItemCount(GetDlgItem(view,15))==4,"Show hidden must reveal remote dotfiles and dotfolders");
            SendMessageW(GetDlgItem(view,16),BM_CLICK,0,0);
            require(ListView_GetItemCount(GetDlgItem(view,11))==2 && ListView_GetItemCount(GetDlgItem(view,15))==2,"Unchecking must filter both panes immediately");
            ListView_SetItemState(GetDlgItem(view,11),-1,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(view,WM_COMMAND,1,0);
            phase=29;waits=0;
        } else if (phase == 29 && ready() && SendMessageW(app.tabs[0]->terminal,WM_APP+121,0,0)==1) {
            auto view=app.tabs[0]->terminal;
            require(ListView_GetItemCount(GetDlgItem(view,15))>=4,"SFTP upload must refresh the remote listing");
            captureTestWindow(app.hwnd,L"ui-sftp.bmp");
            SetWindowTextW(GetDlgItem(view,10),(app.directory+L"\\sftp-download").c_str());SendMessageW(view,WM_COMMAND,9,0);
            LVFINDINFOW find{};find.flags=LVFI_STRING;find.psz=L"   upload-한글.txt";
            int index=ListView_FindItem(GetDlgItem(view,15),-1,&find);require(index>=0,"Uploaded UTF-8 filename must be visible");
            ListView_SetItemState(GetDlgItem(view,15),index,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(view,WM_COMMAND,2,0);
            phase=30;waits=0;
        } else if (phase == 30 && ready() && SendMessageW(app.tabs[0]->terminal,WM_APP+121,0,0)==1) {
            require(std::filesystem::exists(std::filesystem::path(app.directory)/L"sftp-download"/L"upload-한글.txt"),"SFTP download must finish on disk");
            app.command(2);phase=31;waits=0;
        } else if (phase == 31 && ready() && SendMessageW(app.tabs.back()->terminal,WM_APP+121,0,0)==1) {
            require(app.tabs.size()==2 && app.tabs[1]->files,"Duplicate must open another independent SFTP tab");
            closeTestSession(1);closeTestSession(0);
            auto profiles=wook::loadProfiles();auto found=std::find_if(profiles.begin(),profiles.end(),[](auto&p){return p.name==L"Password SSH test";});
            require(found!=profiles.end(),"Missing SFTP password profile");app.connect(*found,true,false,false,true);phase=32;waits=0;
        } else if (phase == 32 && ready() && SendMessageW(app.tabs[0]->terminal,WM_APP+121,0,0)==1) {
            require(true,"SFTP must reuse the scoped DPAPI password");closeTestSession(0);phase=33;waits=0;
        } else if (phase == 33 && app.tabs.empty()) {
            for (int i=0;i<4;++i) app.localShell();
            phase=34;waits=0;
        } else if (phase == 34 && ready() && testSplitFocus(app, require)) {
            if (focusOnly) {
                for (int i = 3; i >= 0; --i) closeTestSession(i);
                phase = 37; waits = 0; return;
            }
            for (int count=2;count<=4;++count) {
                app.setSplit(count);
                require((int)app.panes.size()==count,"Split must display requested number of panes");
                for (int i=0;i<count;++i) {
                    require(IsWindowVisible(app.panes[i]->terminal),"Each split terminal must be visible");
                    RECT actual{};GetWindowRect(app.panes[i]->terminal,&actual);
                    require(actual.right>actual.left+100 && actual.bottom>actual.top+80,"Split terminal must have usable geometry");
                    for (int j=0;j<i;++j) {RECT other{},overlap{};GetWindowRect(app.panes[j]->terminal,&other);require(!IntersectRect(&overlap,&actual,&other),"Split terminals must not overlap");}
                }
            }
            phase=35;waits=0;
        } else if (phase == 35 && ready() && waits>2) {
            RECT splitButton{}, duplicateButton{}, reconnectButton{};
            GetWindowRect(app.control(Split), &splitButton); GetWindowRect(app.control(Duplicate), &duplicateButton);
            GetWindowRect(app.control(Reconnect), &reconnectButton);
            require(!IsWindowVisible(app.control(FilesSession)), "Local CMD must hide Files");
            require(duplicateButton.left-splitButton.right==ui::px(9) && reconnectButton.left-duplicateButton.right==ui::px(9), "Local toolbar must have no Files-sized gap");
            const wchar_t *colors[] = {L"d14d41",L"4385be",L"879a39",L"8b7ec8"};
            for (int i=0;i<4;++i) { app.tabs[i]->profile.alias=L"Server "+std::to_wstring(i+1); app.tabs[i]->profile.tabColor=colors[i]; }
            captureTestWindow(app.hwnd,L"ui-split.bmp");
            for (int i=0;i<4;++i) {
                SetFocus(app.tabs[i]->terminal);
                SendMessageW(app.hwnd,WM_APP+46,(WPARAM)app.tabs[i]->terminal,0);
                require(app.active==i,"Focused pane must own tab commands");
                std::wstring text=L"chcp 65001>nul\rset WSHELL_PANE="+std::to_wstring(i)+L"\recho pane > \""+app.directory+L"\\split-"+std::to_wstring(i)+L".ready\"\r";
                for(wchar_t ch:text)PostMessageW(app.tabs[i]->terminal,WM_CHAR,ch,1);
            }
            phase=36;waits=0;
        } else if (phase == 36 && received(L"split-0.ready") && received(L"split-1.ready") && received(L"split-2.ready") && received(L"split-3.ready")) {
            require(true,"All four panes must send input to independent local shells");
            app.setSplit(2);auto first=app.panes[0];app.select(1);
            require(app.panes.size()==2 && std::find(app.panes.begin(),app.panes.end(),app.tabs[1].get())!=app.panes.end(),"Selecting hidden tab must replace one pane");
            app.setSplit(4);std::swap(app.tabs[0],app.tabs[3]);app.layout();
            require(app.panes.size()==4 && std::find(app.panes.begin(),app.panes.end(),first)!=app.panes.end(),"Tab reorder must retain pane identity");
            app.setSplit(1);int visible=0;for(auto&t:app.tabs)visible+=IsWindowVisible(t->terminal)!=0;
            require(visible==1,"Single view must hide other live terminals");
            app.setSplit(4); app.select(0);
            zoomGroup = app.panes;
            app.zoomPane();
            int zoomVisible = 0; for (auto &tab : app.tabs) zoomVisible += IsWindowVisible(tab->terminal) != 0;
            require(zoomVisible == 1 && app.panes == zoomGroup && !IsWindowVisible(app.control(CommandText)), "Zoom preserves pane group and hides broadcast bar");
            phase=54;waits=0;
        } else if (phase == 54 && waits>3) {
            captureTestWindow(app.hwnd,L"ui-split-zoom.bmp");
            app.zoomPane();
            require(app.panes == zoomGroup && IsWindowVisible(app.control(CommandText)), "Restore keeps the original split group");
            phase=55;waits=0;
        } else if (phase == 55 && waits>3) {
            captureTestWindow(app.hwnd,L"ui-split-restored.bmp");
            app.movePane(2); require(app.tabs[app.active].get() == zoomGroup[1], "Move focus right within split");
            app.movePane(3); require(app.tabs[app.active].get() == zoomGroup[3], "Move focus down within split");
            app.select(0);
            require(SendMessageW(app.control(SendAll),BM_GETCHECK,0,0)==BST_UNCHECKED && SendMessageW(app.control(SyncInput),BM_GETCHECK,0,0)==BST_UNCHECKED,"Broadcast defaults must be off");
            SetWindowTextW(app.control(CommandText),(L"echo single>\""+app.directory+L"\\single-%WSHELL_PANE%.ready\"").c_str()); app.sendCommand();
            phase=38;waits=0;
        } else if (phase == 38 && received(L"single-3.ready") && waits>2) {
            require(!received(L"single-0.ready") && !received(L"single-1.ready") && !received(L"single-2.ready"),"Unchecked command sends only to active pane");
            SendMessageW(app.control(SendAll),BM_CLICK,0,0);
            SetWindowTextW(app.control(CommandText),(L"echo all>\""+app.directory+L"\\all-%WSHELL_PANE%.ready\"").c_str()); app.sendCommand();
            phase=39;waits=0;
        } else if (phase == 39 && received(L"all-0.ready") && received(L"all-1.ready") && received(L"all-2.ready") && received(L"all-3.ready")) {
            require(true,"Common command reaches all four visible terminals");
            SendMessageW(app.control(SyncInput),BM_CLICK,0,0);
            auto target=app.tabs[app.active]->terminal;
            PostMessageW(target,WM_CHAR,3,1);
            phase=41;waits=0;
        } else if (phase == 41 && waits>3) {
            auto target=app.tabs[app.active]->terminal;
            GUITHREADINFO info{sizeof(info)};
            GetGUIThreadInfo(GetWindowThreadProcessId(target,nullptr),&info);
            require(info.hwndFocus==target, "Enabling Sync keyboard must restore terminal keyboard focus");
            for(auto c:std::wstring(L"echo lXveZ"))PostMessageW(target,WM_CHAR,c,1);
            PostMessageW(target,WM_KEYDOWN,VK_BACK,0x000e0001);PostMessageW(target,WM_KEYUP,VK_BACK,0xc00e0001);
            for (int i=0;i<3;++i) {PostMessageW(target,WM_KEYDOWN,VK_LEFT,0x014b0001);PostMessageW(target,WM_KEYUP,VK_LEFT,0xc14b0001);}
            PostMessageW(target,WM_KEYDOWN,VK_DELETE,0x01530001);PostMessageW(target,WM_KEYUP,VK_DELETE,0xc1530001);
            PostMessageW(target,WM_CHAR,'i',1);
            PostMessageW(target,WM_KEYDOWN,VK_HOME,0x01470001);PostMessageW(target,WM_KEYUP,VK_HOME,0xc1470001);
            PostMessageW(target,WM_CHAR,'@',1);
            PostMessageW(target,WM_KEYDOWN,VK_END,0x014f0001);PostMessageW(target,WM_KEYUP,VK_END,0xc14f0001);
            for(auto c:L">\""+app.directory+L"\\live-%WSHELL_PANE%.ready\"\r")PostMessageW(target,WM_CHAR,c,1);
            for(auto c:L"echo 한글\U0001f642>\""+app.directory+L"\\unicode-%WSHELL_PANE%.ready\"\r")PostMessageW(target,WM_IME_CHAR,c,1);
            phase=40;waits=0;
        } else if (phase == 40 && received(L"unicode-0.ready") && received(L"unicode-1.ready") && received(L"unicode-2.ready") && received(L"unicode-3.ready")) {
            require(true,"Live keyboard input, cursor keys and Backspace reach all split terminals without loops");
            for (int i=0;i<4;++i) { std::ifstream file(std::filesystem::path(app.directory)/(L"live-"+std::to_wstring(i)+L".ready")); std::string value;std::getline(file,value);require(value=="live","Broadcast keyboard edits must produce exact text"); }
            for (int i=0;i<4;++i) { std::ifstream file(std::filesystem::path(app.directory)/(L"unicode-"+std::to_wstring(i)+L".ready")); std::string value;std::getline(file,value);require(value==wook::utf8(L"한글\U0001f642"),"Committed Unicode must broadcast once without broken surrogate pairs"); }
            SetWindowTextW(app.control(CommandText),(L"echo once>>\""+app.directory+L"\\once-%WSHELL_PANE%.ready\"").c_str()); app.sendCommand();
            phase=43;waits=0;
        } else if (phase == 43 && received(L"once-0.ready") && received(L"once-1.ready") && received(L"once-2.ready") && received(L"once-3.ready") && waits>2) {
            for (int i=0;i<4;++i) { std::ifstream file(std::filesystem::path(app.directory)/(L"once-"+std::to_wstring(i)+L".ready"));std::string text((std::istreambuf_iterator<char>(file)),{});require(text=="once\n","Common command must not be rebroadcast when live sync is also enabled"); }
            captureTestWindow(app.hwnd,L"ui-broadcast.bmp");
            app.setSplit(2);
            require(SendMessageW(app.control(SendAll),BM_GETCHECK,0,0)==BST_UNCHECKED && SendMessageW(app.control(SyncInput),BM_GETCHECK,0,0)==BST_UNCHECKED,"Changing target panes resets both broadcast modes");
            SendMessageW(app.control(SendAll),BM_CLICK,0,0);
            SetWindowTextW(app.control(CommandText),(L"echo scoped>\""+app.directory+L"\\scoped-%WSHELL_PANE%.ready\"").c_str()); app.sendCommand();
            phase=42;waits=0;
        } else if (phase == 42 && received(L"scoped-3.ready") && received(L"scoped-1.ready") && waits>2) {
            require(!received(L"scoped-0.ready") && !received(L"scoped-2.ready"),"Hidden tabs must not receive common commands");
            app.setSplit(4); app.select(0);
            app.toggleTarget(app.tabs[1].get());
            SendMessageW(app.control(SendAll),BM_CLICK,0,0);
            SendMessageW(app.control(SyncInput),BM_CLICK,0,0);
            SetWindowTextW(app.control(CommandText),(L"echo selected>\""+app.directory+L"\\selected-%WSHELL_PANE%.ready\"").c_str());app.sendCommand();
            auto target=app.tabs[app.active]->terminal;
            for(auto c:L"echo target>\""+app.directory+L"\\target-%WSHELL_PANE%.ready\"\r")PostMessageW(target,WM_CHAR,c,1);
            phase=50;waits=0;
        } else if (phase == 50 && received(L"target-0.ready") && received(L"target-2.ready") && received(L"target-3.ready") && waits>2) {
            require(!received(L"selected-1.ready") && !received(L"target-1.ready"), "Excluded panes receive neither commands nor live keyboard input");
            for (int i : {0,2,3}) require(received((L"selected-"+std::to_wstring(i)+L".ready").c_str()), "Selected panes receive one common command");
            captureTestWindow(app.hwnd,L"ui-broadcast-targets.bmp");
            app.zoomPane(); app.zoomPane();
            require(SendMessageW(app.control(SendAll),BM_GETCHECK,0,0)==BST_UNCHECKED && SendMessageW(app.control(SyncInput),BM_GETCHECK,0,0)==BST_UNCHECKED,"Zoom and restore must stop broadcast modes");
            for(int i=3;i>=0;--i)closeTestSession(i);phase=51;waits=0;
        } else if (phase == 51 && app.tabs.empty()) {
            wchar_t port[32]{}, fingerprint[200]{}; GetEnvironmentVariableW(L"WOOK_TEST_PORT",port,32);
            GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT",fingerprint,200);
            for (auto user : {L"sync-normal",L"sync-application",L"sync-excluded",L"sync-hidden"}) {
                Profile p; p.name=user; p.user=user; p.host=L"127.0.0.1";p.port=_wtoi(port);
                wook::saveProfile(p);
                auto *path=wsPath(L"sessions",wook::utf8(p.name).c_str());auto *store=wsOpen(path,1);free(path);
                require(store != nullptr, "Cannot create broadcast SSH fixture");
                wsSet(store,"SSHManualHostKeys",wook::utf8(fingerprint).c_str());require(wsSave(store),"Cannot pin broadcast fixture");wsClose(store);
                app.connect(p,true);
            }
            phase=52;waits=0;
        } else if (phase == 52 && ready() && received(L"sync-normal.ready") && received(L"sync-application.ready") && received(L"sync-excluded.ready") && received(L"sync-hidden.ready") && waits>3) {
            app.select(0);app.setSplit(3);app.toggleTarget(app.tabs[2].get());
            SendMessageW(app.control(SyncInput),BM_CLICK,0,0);
            auto target=app.tabs[0]->terminal;
            app.active=1; // Simulate input arriving before the queued focus notification.
            SendMessageW(target,WM_CHAR,'x',1);
            require(app.active==0, "First input after focus change must select and broadcast from the focused pane");
            for (auto key : {VK_UP,VK_DOWN,VK_RIGHT,VK_LEFT,VK_HOME,VK_END,VK_DELETE,VK_BACK,VK_TAB,VK_ESCAPE,VK_RETURN}) {
                LPARAM flags=1|(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);
                if (key>=VK_PRIOR && key<=VK_DELETE) flags|=1<<24;
                PostMessageW(target,WM_KEYDOWN,key,flags);PostMessageW(target,WM_KEYUP,key,flags|0xc0000000);
            }
            PostMessageW(target,WM_CHAR,3,1);
            // Paste through the same public terminal command as Ctrl+Shift+V.
            if (OpenClipboard(app.hwnd)) {
                EmptyClipboard(); auto text=GlobalAlloc(GMEM_MOVEABLE,12*sizeof(wchar_t));
                auto *buffer=(wchar_t*)GlobalLock(text);wcscpy_s(buffer,12,L"paste-한글");GlobalUnlock(text);
                SetClipboardData(CF_UNICODETEXT,text);CloseClipboard();
                PostMessageW(target,WM_SYSCOMMAND,0x01A0,0);
            } else throw std::runtime_error("Cannot open clipboard for broadcast paste test");
            phase=53;waits=0;
        } else if (phase == 53 && received(L"sync-input-normal.ready") && received(L"sync-input-application.ready") && waits>3) {
            require(true,"Navigation, editing, control keys and Unicode paste use each SSH terminal's mode");
            require(!received(L"sync-input-excluded.ready") && !received(L"sync-input-hidden.ready"), "Excluded and hidden SSH sessions receive no live input");
            for(int i=3;i>=0;--i)closeTestSession(i);phase=37;waits=0;
        } else if (phase == 37 && app.tabs.empty()) {
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
