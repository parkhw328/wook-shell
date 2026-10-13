// Native list/edit key messages exercise shortcut routing without sending input
// to another application. Files live only in the loopback integration fixture.
static std::function<void(HWND)> sftpShortcutDialogAction;
static std::string sftpShortcutDialogError;
static bool sftpShortcutDialogSeen;
static int sftpShortcutDialogTicks;
static void CALLBACK driveSftpShortcutDialog(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM result) -> BOOL {
        wchar_t kind[80]{}; GetClassNameW(window, kind, 80);
        if (IsWindowVisible(window) && IsWindowEnabled(window) &&
            (!wcscmp(kind, L"wShell.TextPrompt") || !wcscmp(kind, L"wShell.MessageDialog"))) {
            *reinterpret_cast<HWND *>(result) = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&dialog));
    if (!dialog) {
        if (++sftpShortcutDialogTicks > 50) {
            sftpShortcutDialogError = "SFTP shortcut dialog timed out";
            KillTimer(nullptr, timer); PostQuitMessage(1);
        }
        return;
    }
    KillTimer(nullptr, timer); sftpShortcutDialogSeen = true;
    try {
        if (!sftpShortcutDialogAction) throw std::runtime_error("A blocked SFTP shortcut opened a dialog");
        sftpShortcutDialogAction(dialog);
    } catch (const std::exception &e) { sftpShortcutDialogError = e.what(); }
    // Cancel even when an assertion failed; never approve a fixture deletion.
    int cancel = GetDlgItem(dialog, IDNO) ? IDNO : GetDlgItem(dialog, IDCANCEL) ? IDCANCEL : IDOK;
    SendMessageW(dialog, WM_COMMAND, cancel, 0);
}
template<typename Check> bool testSftpShortcuts(App &app, Check require) {
    static int step = 0;
    auto view = app.tabs[0]->terminal;
    auto local = GetDlgItem(view, 11), remote = GetDlgItem(view, 15);
    auto find = [](HWND list, const wchar_t *name) {
        auto text = std::wstring(L"   ") + name;
        LVFINDINFOW match{}; match.flags = LVFI_STRING; match.psz = text.c_str();
        return ListView_FindItem(list, -1, &match);
    };
    auto select = [&](HWND list, const wchar_t *name) {
        int row = find(list, name); require(row >= 0, "Missing SFTP shortcut fixture item");
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    };
    auto press = [&](HWND target, WORD key, std::function<void(HWND)> dialogAction = {}) {
        SetFocus(target); require(GetFocus() == target, "SFTP shortcut must target the intended control");
        sftpShortcutDialogSeen = false; sftpShortcutDialogTicks = 0; sftpShortcutDialogError.clear();
        sftpShortcutDialogAction = dialogAction;
        auto timer = SetTimer(nullptr, 0, 50, driveSftpShortcutDialog);
        require(timer != 0, "Cannot start SFTP shortcut dialog driver");
        SendMessageW(target, WM_KEYDOWN, key, 1);
        SendMessageW(target, WM_KEYUP, key, 0xc0000001);
        KillTimer(nullptr, timer); sftpShortcutDialogAction = {};
        if (!sftpShortcutDialogError.empty()) throw std::runtime_error(sftpShortcutDialogError);
        require(sftpShortcutDialogSeen == bool(dialogAction), "SFTP shortcut must open its expected dialog");
        require(GetFocus() == target, "SFTP shortcut must restore focus to its source control");
    };
    wchar_t remoteFolder[32768]{};
    require(GetEnvironmentVariableW(L"WOOK_TEST_SFTP_ROOT", remoteFolder, 32768) > 0, "Missing loopback SFTP fixture folder");
    auto localMarker = std::filesystem::path(app.directory) / L"sftp-local" / L"shortcut-refresh.txt";
    auto remoteMarker = std::filesystem::path(remoteFolder) / L"shortcut-refresh.txt";
    if (step == 0) {
        select(local, L"empty.txt"); select(remote, L"안녕하세요.txt");
        for (auto list : {local, remote}) {
            press(list, VK_F2, [&](HWND dialog) {
                require(ui::value(dialog) == L"Rename", "F2 must open Rename");
                require(ui::value(GetDlgItem(dialog, 10)) == (list == local ? L"empty.txt" : L"안녕하세요.txt"),
                        "F2 must use the selection from the source pane");
                if (list == remote) captureTestWindow(dialog, L"ui-sftp-shortcut-rename.bmp");
            });
            press(list, VK_DELETE, [&](HWND dialog) {
                require(ui::value(dialog) == L"Delete selected items", "Delete must open the existing confirmation");
                require(GetFocus() == GetDlgItem(dialog, IDNO), "Delete must keep No as the default");
                require(ui::value(GetDlgItem(dialog, 1000)).find(list == local ? L"local pane?" : L"remote pane?") != std::wstring::npos,
                        "Delete must confirm the pane that received the key");
                if (list == remote) captureTestWindow(dialog, L"ui-sftp-shortcut-delete.bmp");
            });
            require(ListView_GetSelectedCount(list) == 1 && SendMessageW(view, WM_APP+121, 0, 0) == 1,
                    "Cancelling rename and delete must preserve selection without starting work");
        }
        // Modified keys must not trigger a plain-key action or clear selection.
        struct KeyboardState {
            BYTE saved[256]{};
            ~KeyboardState() { SetKeyboardState(saved); }
        } keyboard;
        require(GetKeyboardState(keyboard.saved) != FALSE, "Cannot save keyboard state");
        for (int modifier : {VK_CONTROL, VK_SHIFT, VK_MENU}) {
            BYTE state[256]; memcpy(state, keyboard.saved, sizeof(state)); state[modifier] |= 0x80;
            require(SetKeyboardState(state) != FALSE, "Cannot set shortcut modifier state");
            for (WORD key : {VK_F2, VK_DELETE, VK_F5}) press(local, key);
            require(SendMessageW(view, WM_APP+121, 0, 0) == 1 && ListView_GetSelectedCount(local) == 1,
                    "Modified SFTP shortcuts must leave the list and connection idle");
        }
        require(SetKeyboardState(keyboard.saved) != FALSE, "Cannot restore keyboard state");
        for (auto list : {local, remote}) {
            ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
            press(list, VK_F2); press(list, VK_DELETE);
        }
        auto path = GetDlgItem(view, 10); auto originalPath = ui::value(path);
        SetWindowTextW(path, L"abc"); SendMessageW(path, EM_SETSEL, 0, 1);
        press(path, VK_DELETE); press(path, VK_F2); press(path, VK_F5);
        require(ui::value(path) == L"bc" && SendMessageW(view, WM_APP+121, 0, 0) == 1,
                "Delete must edit the path text; file shortcuts must not run in an edit control");
        SetWindowTextW(path, originalPath.c_str());
        require(find(local, L"empty.txt") >= 0 && find(remote, L"안녕하세요.txt") >= 0,
                "Cancelled shortcuts must preserve both fixture files");
        captureTestWindow(app.hwnd, L"ui-sftp-shortcuts.bmp");
        { std::ofstream file(localMarker); file << "local refresh fixture"; }
        { std::ofstream file(remoteMarker); file << "remote refresh fixture"; }
        press(remote, VK_F5);
        require(SendMessageW(view, WM_APP+121, 0, 0) == 0, "F5 must start a remote refresh");
        select(local, L"empty.txt"); select(remote, L"안녕하세요.txt");
        for (auto list : {local, remote}) for (WORD key : {VK_F2, VK_DELETE, VK_F5}) press(list, key);
        require(ListView_GetSelectedCount(local) == 1 && ListView_GetSelectedCount(remote) == 1,
                "Busy shortcuts must neither open dialogs nor refresh the lists");
        ++step; return false;
    }
    if (step == 1) {
        require(find(local, L"shortcut-refresh.txt") >= 0 && find(remote, L"shortcut-refresh.txt") >= 0,
                "F5 from the remote pane must discover external changes in both panes");
        std::filesystem::remove(localMarker); std::filesystem::remove(remoteMarker);
        press(local, VK_F5); ++step; return false;
    }
    require(find(local, L"shortcut-refresh.txt") < 0 && find(remote, L"shortcut-refresh.txt") < 0,
            "F5 from the local pane must remove stale entries from both panes");
    return true;
}
