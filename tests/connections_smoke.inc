std::wstring folderTestName;
bool folderTestAccept = true;
void CALLBACK folderTestTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    auto dialog = FindWindowW(L"wShell.FolderEditor", nullptr);
    if (!dialog) return;
    KillTimer(nullptr, timer);
    SetDlgItemTextW(dialog, 1001, folderTestName.c_str());
    captureTestWindow(dialog, L"ui-folder-editor.bmp");
    PostMessageW(dialog, WM_COMMAND, folderTestAccept ? IDOK : IDCANCEL, 0);
}
void runConnectionsSmoke(App &app) {
    static bool done = false;
    if (done) return;
    done = true;
    int checks = 0;
    auto require = [&](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); ++checks; };
    try {
        Profile first; first.name = L"API 01"; first.host = L"127.0.0.1"; first.user = L"developer"; first.group = L"Production";
        wook::saveProfile(first); app.refresh();
        testWorkspaceChrome(app, require);
        Profile editorHost = first; editorHost.name = L"Folder picker fixture"; editorHost.group = L"production";
        require(editHost(app.hwnd, editorHost, false), "Host editor must save with the folder dropdown");
        require(editorHost.group == L"Production", "Typing an existing folder with different case reuses that folder");
        wook::deleteProfile(editorHost.name); app.refresh();
        auto editor = [&](int action, const std::wstring &text, bool accept = true) {
            folderTestName = text; folderTestAccept = accept;
            auto timer = SetTimer(nullptr, 0, 100, folderTestTimer);
            app.action(action); KillTimer(nullptr, timer);
        };
        auto select = [&](const std::wstring &key) {
            app.filter(key); require(app.connectionSelection() == key, "Connection selection must use a stable host or folder name");
        };
        auto key = [&](WPARAM code) {
            SetFocus(app.control(HostList));
            MSG input{}; input.hwnd = app.control(HostList); input.message = WM_KEYDOWN; input.wParam = code;
            if (!IsDialogMessageW(app.hwnd, &input)) DispatchMessageW(&input);
            while (PeekMessageW(&input, app.hwnd, WM_COMMAND, WM_COMMAND, PM_REMOVE)) DispatchMessageW(&input);
        };
        editor(NewFolder, L"빈 폴더");
        require(app.selectedFolder() == L"빈 폴더" && app.connectionRows.size() == 3, "Create an empty folder through the native dialog");
        require(!IsWindowEnabled(app.control(ConnectHost)) && !IsWindowEnabled(app.control(EditHost)) && !IsWindowEnabled(app.control(FilesHost)),
                "Selecting a folder disables actions that require a host");
        auto count = app.folders.size();
        editor(NewFolder, L"Canceled folder", false);
        require(app.folders.size() == count && wook::loadFolders(wook::loadProfiles()).size() == count, "Canceling folder creation changes no data");
        editor(RenameFolder, L"Staging");
        require(app.selectedFolder() == L"Staging", "Renaming an empty folder preserves its selection");
        editor(RenameFolder, L"Canceled rename", false);
        require(app.selectedFolder() == L"Staging", "Canceling a rename preserves the original folder");
        Profile second = first; second.name = L"API 02"; second.alias = L"API server";
        Profile third = second; third.name = L"API 03";
        Profile root = first; root.name = L"Jump host"; root.group.clear();
        wook::saveProfile(second); wook::saveProfile(third); wook::saveProfile(root);
        app.refresh(); select(L"host:API 03");
        SendMessageW(app.control(MoveHostUp), BM_CLICK, 0, 0);
        require(app.connectionSelection() == L"host:API 03", "Moving a host preserves selection despite duplicate aliases");
        auto profiles = wook::loadProfiles();
        auto position = std::find_if(profiles.begin(), profiles.end(), [](const auto &p) { return p.name == L"API 03"; });
        require(position + 1 != profiles.end() && (position + 1)->name == L"API 02", "Move-up button persists host order");
        SendMessageW(app.control(MoveHostDown), BM_CLICK, 0, 0);
        require(!IsWindowEnabled(app.control(MoveHostDown)), "Last host disables move-down");
        select(L"folder:Production");
        auto rows = app.connectionRows.size();
        key(VK_RETURN);
        require(app.connectionRows.size() == rows - 3 && !app.selectedHost(), "Collapsing a folder hides only its children");
        app.refresh(); require(app.connectionRows.size() == rows - 3, "Collapsed state survives a reload");
        SetWindowTextW(app.control(Search), L"API 03");
        require(app.filtered.size() == 1 && app.selectedHost() && app.selectedHost()->name == L"API 03", "Search reveals a host inside a collapsed folder");
        require(!IsWindowEnabled(app.control(MoveHostUp)) && !IsWindowEnabled(app.control(MoveHostDown)), "Search disables ambiguous order changes");
        auto before = wook::loadProfiles(); app.action(MoveHostUp);
        require(wook::loadProfiles()[0].name == before[0].name, "Reorder action is also guarded while searching");
        SetWindowTextW(app.control(Search), L"");
        require(app.connectionRows.size() == rows - 3, "Clearing search restores collapsed state");
        select(L"folder:Production"); key(VK_RIGHT);
        editor(RenameFolder, L"운영 서버");
        require(app.selectedFolder() == L"운영 서버", "Rename a populated folder through the native dialog");
        for (const auto &p : app.profiles) if (p.name.starts_with(L"API")) require(p.group == L"운영 서버", "All children use the renamed folder");
        app.action(Preview); auto tab = app.tabs.back().get();
        select(L"folder:Staging"); app.action(MoveHostUp);
        require(app.folders.front().name == L"Staging" && app.tabs.back().get() == tab, "Folder reordering preserves open terminal objects");
        wook::moveProfile(L"API 03", L"Staging"); app.refreshConnections(L"host:API 03");
        require(app.selectedHost()->group == L"Staging", "A host can be reassigned to another folder and remain selected");
        select(L"folder:Staging");
        for (int choice : {IDNO, IDYES}) {
            messageTestTicks = 0; messageTestError.clear();
            messageTestAction = [&](HWND dialog) {
                require(GetFocus() == GetDlgItem(dialog, IDNO), "Folder deletion must default to No");
                captureTestWindow(dialog, L"ui-folder-delete.bmp");
                PostMessageW(GetDlgItem(dialog, choice), BM_CLICK, 0, 0);
            };
            messageTestTimer = SetTimer(nullptr, 0, 120, driveMessageTest);
            app.action(DeleteFolder); KillTimer(nullptr, messageTestTimer); messageTestAction = {};
            require(messageTestError.empty(), "Folder deletion confirmation must respond");
            if (choice == IDNO) require(app.selectedFolder() == L"Staging" && app.profiles.size() == 4, "Canceling folder deletion keeps all data and selection");
        }
        auto moved = std::find_if(app.profiles.begin(), app.profiles.end(), [](const auto &p) { return p.name == L"API 03"; });
        require(moved != app.profiles.end() && moved->group.empty() && app.tabs.back().get() == tab, "Deleting a folder retains its hosts and open tabs");
        select(L"host:API 02"); app.refresh();
        require(app.selectedHost() && app.selectedHost()->name == L"API 02", "Refresh preserves the selected host instead of its old row index");
        for (int i = 0; i < 90; ++i) {
            Profile p = first; p.name = L"Server " + std::to_wstring(100 + i); p.group = L"운영 서버"; wook::saveProfile(p);
        }
        app.refresh();
        require(app.filtered.size() == 94, "Large connection collections retain every host");
        auto list = app.control(HostList);
        require(testDarkScrollbars(list, require) == 1, "Grouped navigation retains the dark native scrollbar");
        SendMessageW(list, WM_VSCROLL, SB_LINEDOWN, 0);
        require(SendMessageW(list, LB_GETTOPINDEX, 0, 0) > 0, "Grouped lists scroll with many hosts");
        SetWindowTextW(app.control(Search), L"Server 189");
        require(app.selectedHost() && app.selectedHost()->name == L"Server 189", "Search can find a late host in a large group");
        SetWindowTextW(app.control(Search), L"");
        SendMessageW(list, LB_SETTOPINDEX, 0, 0); select(L"folder:운영 서버");
        captureTestWindow(app.hwnd, L"ui-connections-expanded.bmp");
        app.action(ToggleFolder); captureTestWindow(app.hwnd, L"ui-connections-collapsed.bmp");
        SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(980), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (int id : {NewFolder, MoveHostUp, MoveHostDown}) {
            RECT r{}; GetWindowRect(app.control(id), &r); MapWindowPoints(nullptr, app.hwnd, (POINT *)&r, 2);
            require(IsWindowVisible(app.control(id)) && r.left >= 0 && r.right <= ui::px(sidebarExpanded), "Folder controls fit the minimum window width");
        }
        captureTestWindow(app.hwnd, L"ui-connections-minimum.bmp");
        SetFocus(app.control(NewFolder)); app.setNavigation(false);
        require(!IsWindowVisible(app.control(NewFolder)) && !IsWindowVisible(app.control(MoveHostUp)) && !IsWindowVisible(app.control(MoveHostDown)),
                "Hidden navigation removes its folder controls");
        app.setNavigation(true);
        std::ofstream result(std::filesystem::path(app.directory) / L"connections-result.json");
        result << "{\"passed\":true,\"checks\":" << checks << "}";
    } catch (const std::exception &error) {
        std::ofstream result(std::filesystem::path(app.directory) / L"connections-result.json");
        result << "{\"passed\":false,\"checks\":" << checks << ",\"error\":\"" << error.what() << "\"}";
    }
    DestroyWindow(app.hwnd);
}
