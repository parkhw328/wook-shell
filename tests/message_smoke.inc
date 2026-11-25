static std::function<void(HWND)> messageTestAction;
static std::string messageTestError;
static UINT_PTR messageTestTimer;
static int messageTestTicks;
static void CALLBACK driveMessageTest(HWND, UINT, UINT_PTR, DWORD) {
    try {
        if (++messageTestTicks > 70) throw std::runtime_error("Message dialog driver timed out");
        HWND dialog{};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM result) -> BOOL {
            wchar_t kind[80]{}; GetClassNameW(window, kind, 80);
            if (IsWindowVisible(window) && IsWindowEnabled(window) && !wcscmp(kind, L"wShell.MessageDialog")) {
                *reinterpret_cast<HWND *>(result) = window; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&dialog));
        if (dialog) messageTestAction(dialog);
    } catch (const std::exception &e) {
        messageTestError = e.what(); KillTimer(nullptr, messageTestTimer);
        PostQuitMessage(1);
    }
}
template<typename Check> void testMessageDialogs(App &app, Check require) {
    ShowWindow(app.hwnd, SW_SHOWNORMAL); SetForegroundWindow(app.hwnd); SetFocus(app.control(Search));
    auto inspect = [&](HWND dialog) {
        require(!IsWindowEnabled(app.hwnd), "The message owner must be disabled until the choice returns");
        RECT area{}; GetClientRect(dialog, &area); std::vector<RECT> rectangles;
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!IsWindowVisible(child)) continue;
            RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, dialog, (POINT *)&r, 2);
            require(r.left >= 0 && r.top >= 0 && r.right <= area.right && r.bottom <= area.bottom,
                    "Message controls must fit in the client area");
            for (const auto &other : rectangles) { RECT overlap{}; require(!IntersectRect(&overlap, &r, &other), "Message controls must not overlap"); }
            rectangles.push_back(r);
            LOGFONTW font{}; GetObjectW((HFONT)SendMessageW(child, WM_GETFONT, 0, 0), sizeof(font), &font);
            require(!wcscmp(font.lfFaceName, L"JetBrains Mono"), "Messages must use the bundled typeface");
        }
    };
    auto run = [&](std::function<int()> open, std::function<void(HWND)> action, int expected, HWND expectedFocus = nullptr) {
        messageTestTicks = 0; messageTestError.clear(); messageTestAction = action;
        messageTestTimer = SetTimer(nullptr, 0, 120, driveMessageTest);
        require(messageTestTimer != 0, "Cannot start message test driver");
        int result = open(); KillTimer(nullptr, messageTestTimer); messageTestAction = {};
        if (!messageTestError.empty()) throw std::runtime_error(messageTestError);
        require(result == expected, "Message result must preserve the chosen action");
        require(IsWindowEnabled(app.hwnd) && GetFocus() == (expectedFocus ? expectedFocus : app.control(Search)), "Closing a message must restore owner and focus");
    };
    run([&] { wsShowAbout(app.hwnd); return IDOK; }, [&](HWND dialog) {
        inspect(dialog);
        require(ui::value(dialog).find(L"wShell ") == 0, "About must show the current product version");
        const wchar_t *urls[] = {L"mailto:parkhw328@gmail.com", L"https://github.com/parkhw328/wook-shell"};
        const int links[] = {1010, 1012};
        for (int i = 0; i < 2; ++i) {
            auto target = (const wchar_t *)GetPropW(GetDlgItem(dialog, links[i]), L"wShell.LinkTarget");
            require(target && !wcscmp(target, urls[i]), "About links must point to the intended maintainer and repository");
        }
        require(!GetDlgItem(dialog, 1011) && !GetDlgItem(dialog, 1001), "About must not repeat GitHub or contain the shortcuts button");
        captureTestWindow(dialog, L"ui-about.bmp");
        PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
    }, IDOK);
    for (WORD key : {WORD('Y'), WORD('N'), WORD(VK_ESCAPE), WORD(VK_RETURN)}) {
        run([&] { return wsWorkspaceCloseDialog(app.hwnd); }, [&](HWND dialog) {
            inspect(dialog);
            require(ui::value(dialog)==L"Close workspace", "Workspace confirmation must retain its title");
            require(ui::value(GetDlgItem(dialog,IDYES))==L"Yes (Y)" && ui::value(GetDlgItem(dialog,IDNO))==L"No (N)",
                    "Workspace confirmation must show Y and N shortcuts");
            require(GetFocus()==GetDlgItem(dialog,IDNO), "Workspace close must default to No");
            if(key=='Y')captureTestWindow(dialog,L"ui-workspace-close-shortcuts.bmp");
            // A shortcut must work even with the opposite button focused.
            if(key=='N')SetFocus(GetDlgItem(dialog,IDYES));
            PostMessageW(GetFocus(),WM_KEYDOWN,key,1);
        }, key=='Y'?IDYES:IDNO);
    }
    int repeatStage=0;
    run([&] { return wsWorkspaceCloseDialog(app.hwnd); }, [&](HWND dialog) {
        if(!repeatStage++) {
            PostMessageW(GetFocus(),WM_KEYDOWN,'Y',1|(1LL<<30)); return;
        }
        require(IsWindow(dialog), "Held Y must not approve workspace closure");
        PostMessageW(GetFocus(),WM_KEYDOWN,'N',1);
    }, IDNO);
    RECT original{}; GetWindowRect(app.hwnd, &original);
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(980), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    auto helpMenu = GetSubMenu(GetMenu(app.hwnd), 5);
    require(helpMenu && GetMenuItemID(helpMenu, 0) == Shortcuts && GetMenuItemID(helpMenu, 3) == About,
            "Help must group keyboard shortcuts, licenses and About");
    captureTestWindow(app.hwnd, L"ui-menu-bar.bmp", true);
    run([&] { SendMessageW(app.hwnd, WM_COMMAND, Shortcuts, 0); return IDOK; }, [&](HWND dialog) {
        inspect(dialog);
        require(ui::value(dialog) == L"Keyboard shortcuts" && ui::value(GetDlgItem(dialog, 1000)).find(L"Ctrl+Shift+T") != std::wstring::npos,
                "Help menu command must open the keyboard reference");
        captureTestWindow(dialog, L"ui-shortcuts.bmp"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
    }, IDOK, app.control(Search));
    SetWindowPos(app.hwnd, nullptr, 0, 0, original.right-original.left, original.bottom-original.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetFocus(app.control(Search));
    run([&] { return wsMessageBoxW(app.hwnd, L"The selected file could not be saved.\r\nCheck the destination and try again.\r\n\r\n한글 오류 안내 / C:\\test\\output.txt", L"Unable to save file", MB_OK | MB_ICONERROR); }, [&](HWND dialog) {
        inspect(dialog); require(GetFocus() == GetDlgItem(dialog, IDOK), "OK must be the alert default");
        require(ui::value(GetDlgItem(dialog, 1000)).find(L"한글 오류 안내") != std::wstring::npos, "Message text must preserve Unicode");
        captureTestWindow(dialog, L"ui-alert.bmp"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
    }, IDOK);
    run([&] { return wsMessageBoxW(app.hwnd, L"Delete this saved host? Existing terminal tabs will stay open.", L"Delete host", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2); }, [&](HWND dialog) {
        inspect(dialog); require(GetFocus() == GetDlgItem(dialog, IDNO), "Destructive confirmation must retain the No default");
        captureTestWindow(dialog, L"ui-confirm.bmp"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
    }, IDNO);
    struct ShortcutCase { UINT flags; WPARAM key; int result; const wchar_t *label; };
    const ShortcutCase shortcuts[] = {
        {MB_OK, 'O', IDOK, L"OK (O)"},
        {MB_OKCANCEL, 'C', IDCANCEL, L"Cancel (C)"},
        {MB_YESNO | MB_DEFBUTTON2, 'Y', IDYES, L"Yes (Y)"},
        {MB_YESNO, 'N', IDNO, L"No (N)"},
        {MB_YESNOCANCEL, 'C', IDCANCEL, L"Cancel (C)"},
        {MB_RETRYCANCEL, 'R', IDRETRY, L"Retry (R)"},
        {MB_ABORTRETRYIGNORE, 'A', IDABORT, L"Abort (A)"},
        {MB_ABORTRETRYIGNORE, 'I', IDIGNORE, L"Ignore (I)"},
        {MB_CANCELTRYCONTINUE, 'T', IDTRYAGAIN, L"Try again (T)"},
        {MB_CANCELTRYCONTINUE, 'U', IDCONTINUE, L"Continue (U)"}
    };
    for (const auto &test : shortcuts) for (UINT message : {UINT(WM_KEYDOWN), UINT(WM_SYSKEYDOWN)}) {
        int shortcutStage = 0;
        run([&] { return wsMessageBoxW(app.hwnd, L"Keyboard shortcut fixture", L"Confirmation", test.flags); }, [&](HWND dialog) {
            inspect(dialog);
            require(ui::value(GetDlgItem(dialog, test.result)) == test.label, "Every common choice must display its shortcut");
            SetFocus(GetDlgItem(dialog, 1000));
            if (!shortcutStage++) {
                PostMessageW(GetFocus(), message, test.key, 1 | (1LL << 30)); return;
            }
            require(IsWindow(dialog), "Repeated shortcut must not choose an action");
            PostMessageW(GetFocus(), message, test.key, 1);
        }, test.result);
    }
    int stage = 0;
    run([&] { return wsMessageBoxW(app.hwnd, L"Choose Yes or No.", L"Choice semantics", MB_YESNO | MB_DEFBUTTON2); }, [&](HWND dialog) {
        if (!stage++) { PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0); return; }
        if (stage == 2) { PostMessageW(dialog, WM_CLOSE, 0, 0); return; }
        require(IsWindow(dialog), "Yes/No without a Cancel button must not accept Escape or close");
        PostMessageW(GetDlgItem(dialog, IDYES), BM_CLICK, 0, 0);
    }, IDYES);
    std::wstring longText;
    for (int i = 0; i < 100; ++i) longText += L"Long diagnostic line 한글 " + std::to_wstring(i) + L"\r\n";
    stage = 0;
    run([&] { return wsMessageBoxW(app.hwnd, longText.c_str(), L"Connection details", MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONWARNING); }, [&](HWND dialog) {
        inspect(dialog); require(ui::value(GetDlgItem(dialog, 1000)) == longText, "Long messages must retain all text in the scrollable body");
        require(GetFocus() == GetDlgItem(dialog, IDCANCEL), "Third-button default must be preserved");
        HWND body = GetDlgItem(dialog, 1000);
        if (stage == 0) {
            SendMessageW(body, WM_MOUSEWHEEL, MAKEWPARAM(0, -60), 0);
            require(SendMessageW(body, EM_GETFIRSTVISIBLELINE, 0, 0) == 0, "Partial wheel motion must accumulate");
            ++stage; return;
        }
        if (stage == 1) {
            SendMessageW(body, WM_MOUSEWHEEL, MAKEWPARAM(0, -60), 0);
            UINT lines{}; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines) require(SendMessageW(body, EM_GETFIRSTVISIBLELINE, 0, 0) > 0, "Wheel must scroll long dialog text");
            SendMessageW(body, EM_LINESCROLL, 0, -1000);
            RECT r{}; GetWindowRect(body, &r); MapWindowPoints(nullptr, dialog, (POINT *)&r, 2);
            int x = r.right + ui::px(14);
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, r.top + ui::px(6)));
            SendMessageW(dialog, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, r.bottom - ui::px(6)));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(x, r.bottom - ui::px(6)));
            require(SendMessageW(body, EM_GETFIRSTVISIBLELINE, 0, 0) > 50, "Dragging the themed thumb must reach later diagnostic text");
            ++stage; return;
        }
        captureTestWindow(dialog, L"ui-long-alert.bmp"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
    }, IDCANCEL);
    stage = 0;
    run([&] { return wsMessageBoxW(app.hwnd, L"A message must remain readable when moving between displays.", L"Display scaling", MB_OKCANCEL | MB_DEFBUTTON2); }, [&](HWND dialog) {
        if (!stage++) {
            RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
            RECT size{area.left+20,area.top+20,std::min(area.right-20,area.left+1010),std::min(area.bottom-20,area.top+620)};
            SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(144,144), (LPARAM)&size); return;
        }
        inspect(dialog);
        LOGFONTW font{}; GetObjectW((HFONT)SendDlgItemMessageW(dialog, IDOK, WM_GETFONT, 0, 0), sizeof(font), &font);
        require(font.lfHeight == -22, "DPI changes must rescale message control fonts");
        captureTestWindow(dialog, L"ui-message-150-percent.bmp"); PostMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
    }, IDCANCEL);
    stage = 0; HWND hostDialog{};
    run([&] { return wsHostKeyDialog(app.hwnd, "Server identity has changed", "WARNING: the server key differs from the saved key.\r\nSHA256:test-only-fingerprint", "Key type: ssh-ed25519\r\nSHA256:test-only-fingerprint", 1); }, [&](HWND dialog) {
        if (!stage) {
            inspect(dialog); require(GetFocus() == GetDlgItem(dialog, IDCANCEL), "Host trust must default to Cancel");
            hostDialog = dialog; captureTestWindow(dialog, L"ui-host-key-warning.bmp");
            ++stage; PostMessageW(GetDlgItem(dialog, 1001), BM_CLICK, 0, 0);
        } else if (stage == 1 && dialog != hostDialog) {
            require(!IsWindowEnabled(hostDialog), "Nested details must keep the trust decision modal");
            require(ui::value(GetDlgItem(dialog, 1000)).find(L"ssh-ed25519") != std::wstring::npos, "Host key details must remain readable");
            ++stage; PostMessageW(dialog, WM_CLOSE, 0, 0);
        } else if (stage == 2 && dialog == hostDialog) {
            require(IsWindowEnabled(hostDialog) && !IsWindowEnabled(app.hwnd), "Closing nested details must not re-enable the main owner");
            PostMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
        }
    }, IDCANCEL);
    for (int result : {IDYES, IDNO}) run([&] { return wsHostKeyDialog(app.hwnd, "Unknown server key", "Fixture fingerprint", "Fixture details", 0); },
        [&](HWND dialog) { PostMessageW(GetDlgItem(dialog, result), BM_CLICK, 0, 0); }, result);
}

std::vector<HWND> workspaceEngineDialogs(HWND workspace) {
    std::vector<HWND> dialogs;
    HWND dialog = nullptr;
    while ((dialog = FindWindowExW(nullptr, dialog, L"wShell.MessageDialog", nullptr))) {
        DWORD pid{}; GetWindowThreadProcessId(dialog, &pid);
        if (pid != GetCurrentProcessId() && GetWindow(dialog, GW_OWNER) == workspace && IsWindowVisible(dialog))
            dialogs.push_back(dialog);
    }
    return dialogs;
}
template<typename Check> bool testConcurrentConnectionDialogs(App &app, Check require) {
    static int step = 0, waits = 0;
    static HWND sshDialog{}, filesDialog{};
    if (++waits > 60) throw std::runtime_error("Parallel SSH/SFTP dialogs timed out at step " + std::to_string(step));
    if (step == 0) {
        // Keep the accepted SSH session on server 2 live. Server 1 was trusted
        // only once, so both new connections must make their own decision.
        auto found = std::find_if(app.profiles.begin(), app.profiles.end(),
            [](const Profile &p) { return p.name == L"Dialog host 1"; });
        require(found != app.profiles.end(), "Missing Connect Once host");
        Profile p = *found;
        app.connect(p, true);
        app.connect(p, true, false, false, true);
    } else if (step == 1) {
        auto dialogs = workspaceEngineDialogs(app.hwnd);
        if (dialogs.size() != 2) return false;
        for (auto dialog : dialogs) {
            require(GetDlgItem(dialog, IDYES) && GetDlgItem(dialog, IDNO), "Both workers must present host key choices");
            DWORD pid{}; GetWindowThreadProcessId(dialog, &pid);
            if (pid == app.tabs[1]->pid) sshDialog = dialog;
            else filesDialog = dialog;
        }
        require(sshDialog && filesDialog && IsWindowEnabled(app.hwnd), "Both decisions must leave other tabs usable");
        require(IsWindowEnabled(app.tabs[0]->terminal) && !IsWindowEnabled(app.tabs[1]->terminal),
            "Only the SSH terminal awaiting a decision may be disabled");
        app.select(0);
        PostMessageW(app.tabs[0]->terminal, WM_CHAR, L'x', 1);
    } else if (step == 2) {
        if (!std::filesystem::exists(std::filesystem::path(app.directory) / L"dialog-existing-input.ready")) return false;
        require(IsWindow(sshDialog) && IsWindow(filesDialog), "Both connections must still await a trust decision");
        PostMessageW(filesDialog, WM_COMMAND, IDNO, 0);
    } else if (step == 3) {
        auto view = app.tabs[2]->terminal;
        if (!SendMessageW(view, WM_APP + 121, 0, 0)) return false;
        require(ListView_GetItemCount(GetDlgItem(view, 15)) == 1, "SFTP must list the fixture while SSH still waits");
        require(IsWindowEnabled(sshDialog), "Answering SFTP must preserve the SSH decision");
        PostMessageW(sshDialog, WM_COMMAND, IDNO, 0);
    } else if (step == 4) {
        if (!std::filesystem::exists(std::filesystem::path(app.directory) / L"dialog-second-shell.ready")) return false;
        // Same trusted host and account as the live tab, but this server refuses SFTP.
        app.connect(app.tabs[0]->profile, true, false, false, true);
    } else if (step == 5) {
        auto dialogs = workspaceEngineDialogs(app.hwnd);
        if (dialogs.empty()) return false;
        require(dialogs.size() == 1 && IsWindowEnabled(app.hwnd), "A failed subsystem must leave the workspace usable");
        wchar_t body[512]{};
        SendDlgItemMessageW(dialogs.front(), 1000, WM_GETTEXT, std::size(body), (LPARAM)body);
        require(wcsstr(body, L"Server refused to start") != nullptr, "SFTP must display the server's refusal");
        PostMessageW(dialogs.front(), WM_COMMAND, IDOK, 0);
    } else if (step == 6) {
        auto state = SendMessageW(app.tabs[3]->terminal, WM_APP + 121, 0, 0);
        if (!state) return false;
        auto reconnect = GetDlgItem(app.tabs[3]->terminal, 6);
        require(state == -1 && IsWindowEnabled(reconnect), "A refused SFTP session must finish and allow retry");
        SendMessageW(reconnect, BM_CLICK, 0, 0);
    } else {
        auto view = app.tabs[3]->terminal;
        auto state = SendMessageW(view, WM_APP + 121, 0, 0);
        if (!state) return false;
        require(state == 1 && ListView_GetItemCount(GetDlgItem(view, 15)) == 1, "The same tab must reconnect and load its directory");
        require(IsWindowEnabled(app.tabs[1]->terminal) && app.inputReady(app.tabs[0].get()) && app.inputReady(app.tabs[1].get()) &&
            SendMessageW(app.tabs[2]->terminal, WM_APP + 121, 0, 0) == 1, "Other connections must survive the refusal");
        for (int i = 3; i >= 0; --i) app.closeTab(i, true);
        return true;
    }
    ++step; waits = 0; return false;
}
template<typename Check> bool testHostKeyMessages(App &app, Check require) {
    static int index = 0, step = 0, waits = 0;
    if (index == 3) return testConcurrentConnectionDialogs(app, require);
    auto file = [&](const std::wstring &name) { return std::filesystem::path(app.directory) / name; };
    if (++waits > 60) throw std::runtime_error("SSH host key dialog test timed out");
    if (step == 0) {
        wchar_t port[32]{}; auto key = L"WOOK_DIALOG_PORT_" + std::to_wstring(index);
        require(GetEnvironmentVariableW(key.c_str(), port, 32), "Missing host key fixture port");
        Profile p; p.name = L"Dialog host " + std::to_wstring(index); p.host = L"127.0.0.1"; p.user = L"dialog-user"; p.port = _wtoi(port);
        wook::saveProfile(p); app.connect(p, true); step = 1; waits = 0;
    } else if (step == 1 && !app.tabs.empty()) {
        HWND dialog{};
        for (HWND candidate = GetTopWindow(nullptr); candidate; candidate = GetWindow(candidate, GW_HWNDNEXT)) {
            DWORD pid{}; GetWindowThreadProcessId(candidate, &pid); wchar_t kind[80]{}; GetClassNameW(candidate, kind, 80);
            if (pid == app.tabs[0]->pid && IsWindowVisible(candidate) && !wcscmp(kind, L"wShell.MessageDialog")) { dialog = candidate; break; }
        }
        if (!dialog) return false;
        require(ui::value(GetDlgItem(dialog, IDYES)) == L"Accept" && ui::value(GetDlgItem(dialog, IDNO)) == L"Connect Once",
                "Real SSH must show all host trust choices");
        require(ui::value(GetDlgItem(dialog, 1001)) == L"More info", "Real SSH details button must have a visible caption");
        HWND body = GetDlgItem(dialog, 1000);
        int length = (int)SendMessageW(body, WM_GETTEXTLENGTH, 0, 0);
        std::wstring bodyText(length + 1, L'\0');
        SendMessageW(body, WM_GETTEXT, bodyText.size(), (LPARAM)bodyText.data()); bodyText.resize(length);
        std::ofstream(file(L"dialog-host-text.txt")) << wook::utf8(bodyText);
        require(bodyText.find(L"SHA256:") != std::wstring::npos, "Real server fingerprint must remain visible");
        GUITHREADINFO info{sizeof(info)}; GetGUIThreadInfo(GetWindowThreadProcessId(dialog, nullptr), &info);
        require(info.hwndFocus == GetDlgItem(dialog, IDCANCEL), "Real SSH trust must default to Cancel");
        captureTestWindow(dialog, L"ui-host-key.bmp");
        PostMessageW(GetDlgItem(dialog, index == 0 ? IDCANCEL : index == 1 ? IDNO : IDYES), BM_CLICK, 0, 0);
        step = 2; waits = 0;
    } else if (step == 2 && std::filesystem::exists(file(L"dialog-finished-" + std::to_wstring(index) + L".ready")) && waits > 3) {
        auto path = wsPath(L"trust", "hostkeys"); auto store = wsOpen(path, 0); free(path);
        int keys = 0; if (store) { for (auto pair = store->pairs; pair; pair = pair->next) ++keys; wsClose(store); }
        require(keys == (index == 2 ? 1 : 0), "Only Accept may persist an SSH host key");
        if (index == 2) { ++index; return false; }
        app.closeTab(0, true); step = 3; waits = 0;
    } else if (step == 3 && app.tabs.empty()) {
        ++index; step = 0; waits = 0;
    }
    return false;
}
