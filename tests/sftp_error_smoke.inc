// Exercise Enter through the workspace dialog routing, not a Go command.
template<typename Check> bool testSftpPathEnter(App &app, HWND view, Check require) {
    static int step=0;
    auto directory=std::filesystem::path(app.directory)/L"downloads";
    auto local=GetDlgItem(view,10), remote=GetDlgItem(view,14);
    auto enter=[&](HWND edit, bool repeat=false) {
        if(IsWindowEnabled(edit))SetFocus(edit);
        MSG message{};message.hwnd=edit;message.message=WM_KEYDOWN;message.wParam=VK_RETURN;
        message.lParam=1|(repeat?(1LL<<30):0);
        require(SendMessageW(edit,WM_GETDLGCODE,VK_RETURN,(LPARAM)&message)&DLGC_WANTMESSAGE,
                "Path edit must claim Enter from dialog navigation");
        if(!IsDialogMessageW(app.hwnd,&message))DispatchMessageW(&message);
        SendMessageW(edit,WM_CHAR,L'\r',1);
    };
    if(step==0) {
        auto nested=directory/L"로컬 폴더";std::filesystem::create_directory(nested);
        {std::ofstream file(nested/L"local-marker.txt");file<<"local";}
        SetWindowTextW(remote,L"/not-submitted");SetWindowTextW(local,nested.c_str());
        enter(local);
        require(ListView_GetItemCount(GetDlgItem(view,11))==1 && ui::value(remote)==L"/not-submitted",
                "Local Enter must navigate only the left pane");
        require(GetFocus()==GetDlgItem(view,11),"Successful Local Enter must move focus from the path caret to its file list");
        SetWindowTextW(local,directory.c_str());enter(local,true);
        require(ListView_GetItemCount(GetDlgItem(view,11))==1,"Held Enter must not repeatedly navigate");
        SetWindowTextW(local,nested.c_str());SetWindowTextW(remote,L"/remote-folder");enter(remote);
        require(SendMessageW(view,WM_APP+121,0,0)==0,"Remote Enter must start remote navigation");
        SetWindowTextW(local,directory.c_str());enter(local);
        require(ListView_GetItemCount(GetDlgItem(view,11))==1,"Busy Enter must not navigate the local pane");
        SetWindowTextW(local,nested.c_str());++step;return false;
    }
    if(step==1) {
        require(ui::value(remote)==L"/remote-folder" && ListView_GetItemCount(GetDlgItem(view,15))==1,
                "Remote Enter must display the requested remote folder");
        require(ListView_GetItemCount(GetDlgItem(view,11))==1,"Remote Enter must preserve the local listing");
        require(GetFocus()==GetDlgItem(view,15),("Successful Remote Enter must focus its file list; focus id="+
                std::to_string(GetDlgCtrlID(GetFocus()))+", visible="+std::to_string(IsWindowVisible(view))).c_str());
        captureTestWindow(app.hwnd,L"ui-sftp-path-focus.bmp");
        SetWindowTextW(remote,L"/missing-navigation-folder");enter(remote);step=2;return false;
    }
    if(step==2) {
        require(GetFocus()==remote && ui::value(remote)==L"/missing-navigation-folder",
                "Failed Remote Enter must retain the path and focus for correction");
        SetWindowTextW(remote,L"/");enter(remote);++step;return false;
    }
    if(step==3) {
        require(ui::value(remote)==L"/","Remote Enter must return to root");
        SetWindowTextW(remote,L"/remote-folder");enter(remote);
        SetFocus(GetDlgItem(view,11));++step;return false;
    }
    if(step==4) {
        require(GetFocus()==GetDlgItem(view,11),"Remote completion must not steal focus after the user switches panes");
        SetWindowTextW(remote,L"/");enter(remote);++step;return false;
    }
    require(ui::value(remote)==L"/","Remote Enter must return to root");
    SetWindowTextW(local,directory.c_str());enter(local);
    require(ListView_GetItemCount(GetDlgItem(view,11))==1,"Local Enter must return to downloads");
    std::filesystem::remove_all(directory/L"로컬 폴더");
    // Refresh the local list after removing our isolated fixture.
    enter(local);require(ListView_GetItemCount(GetDlgItem(view,11))==0,"Local Enter must refresh the listing");
    return true;
}

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
        if(!testSftpPathEnter(app,view,require))return false;
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
