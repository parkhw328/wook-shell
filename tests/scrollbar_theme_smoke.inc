template<typename Check> void testScrollbarThemes(App &app, Check require) {
    auto profiles = app.profiles;
    bool navigation = app.navigationVisible;
    auto query = ui::value(app.control(Search));
    SetWindowTextW(app.control(Search), L""); app.setNavigation(true);
    for (int i = 0; i < 100; ++i) {
        Profile p; p.name = L"Scrollbar fixture " + std::to_wstring(i); p.host = L"127.0.0.1";
        app.profiles.push_back(p);
    }
    app.filter();
    auto hosts = app.control(HostList);
    require(testDarkScrollbars(hosts, require) == 1, "Overflowing host navigation must show a dark vertical scrollbar");
    SendMessageW(hosts, WM_VSCROLL, SB_LINEDOWN, 0);
    require(SendMessageW(hosts, LB_GETTOPINDEX, 0, 0) > 0, "Host scrollbar must still scroll the native list");
    captureTestWindow(app.hwnd, L"ui-scrollbar-navigation.bmp");
    // Clear indices before restoring the smaller profile vector.
    SendMessageW(hosts, LB_SETCURSEL, (WPARAM)-1, 0); app.filtered.clear();
    app.profiles = std::move(profiles); SetWindowTextW(app.control(Search), query.c_str()); app.filter(); app.setNavigation(navigation);

    auto fixture = CreateWindowExW(0, L"STATIC", L"wShell scrollbar fixtures", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        40, 40, ui::px(700), ui::px(500), app.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(fixture != nullptr, "Cannot create scrollbar fixture");
    struct Close { HWND window; ~Close() { DestroyWindow(window); } } close{fixture};
    wsThemeEventLog(fixture); ShowWindow(fixture, SW_SHOWNORMAL);
    std::wstring content;
    for (int i = 0; i < 100; ++i) content += std::wstring(180, L'x') + L"\r\n";
    for (auto kind : {L"LISTBOX", L"EDIT", WC_LISTVIEWW}) {
        bool list = !wcscmp(kind, L"LISTBOX"), edit = !wcscmp(kind, L"EDIT");
        DWORD style = WS_TABSTOP | (list ? LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL :
            edit ? ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL : LVS_REPORT);
        auto control = ui::control(fixture, kind, L"", 100, style);
        ui::place(control, 20, 20, 400, 220);
        // Populate after applying the shared theme: bars must appear dark later.
        if (list) {
            for (int i = 0; i < 100; ++i) SendMessageW(control, LB_ADDSTRING, 0, (LPARAM)L"Scrollbar fixture row");
            SendMessageW(control, LB_SETHORIZONTALEXTENT, ui::px(1400), 0);
        } else if (edit) SetWindowTextW(control, content.c_str());
        else {
            ListView_SetBkColor(control, ui::bg); ListView_SetTextBkColor(control, ui::bg); ListView_SetTextColor(control, ui::text);
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = (wchar_t *)L"File name"; column.cx = ui::px(1400);
            ListView_InsertColumn(control, 0, &column);
            for (int i = 0; i < 100; ++i) {
                LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = i; item.pszText = (wchar_t *)L"Scrollbar fixture row";
                ListView_InsertItem(control, &item);
            }
        }
        for (int width : {400, 520}) {
            ui::place(control, 20, 20, width, 220);
            require(testDarkScrollbars(control, require) == 3, "Overflow fixtures must expose both scrollbars and their corner");
            SendMessageW(control, WM_VSCROLL, SB_LINEDOWN, 0); SendMessageW(control, WM_HSCROLL, SB_LINERIGHT, 0);
            require(GetScrollPos(control, SB_VERT) > 0 && GetScrollPos(control, SB_HORZ) > 0, "Dark bars must retain both native scrolling directions");
            SendMessageW(control, WM_THEMECHANGED, 0, 0);
            require(testDarkScrollbars(control, require) == 3, "Dark scrollbars must survive resize and theme refresh");
        }
        captureTestWindow(fixture, list ? L"ui-scrollbar-list.bmp" : edit ? L"ui-scrollbar-text.bmp" : L"ui-scrollbar-files.bmp");
        DestroyWindow(control);
    }
    for (auto mode : {CBS_DROPDOWN, CBS_DROPDOWNLIST}) {
        auto combo = ui::control(fixture, L"COMBOBOX", L"", 101, mode | WS_VSCROLL | WS_TABSTOP);
        ui::place(combo, 20, 20, 400, 200);
        for (int i = 0; i < 100; ++i) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Dropdown scrollbar fixture");
        SendMessageW(combo, CB_SETCURSEL, 0, 0); SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
        COMBOBOXINFO info{sizeof(info)};
        require(GetComboBoxInfo(combo, &info) && IsWindowVisible(info.hwndList), "Combo popup must be visible");
        require(testDarkScrollbars(info.hwndList, require) == 1, "Combo popup must use the shared dark scrollbar");
        SendMessageW(info.hwndList, WM_VSCROLL, SB_LINEDOWN, 0);
        require(SendMessageW(combo, CB_GETTOPINDEX, 0, 0) > 0, "Dropdown scrolling must remain functional");
        SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0); DestroyWindow(combo);
    }
}
