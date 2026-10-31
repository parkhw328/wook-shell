// Isolated navigation gestures and tooltip routing; no SSH processes are started.
struct NavigationPaintProbe {
    HWND window;
    unsigned paints = 0, moves = 0, captions = 0;
    explicit NavigationPaintProbe(HWND value) : window(value) { SetWindowSubclass(window, proc, 91, (DWORD_PTR)this); }
    ~NavigationPaintProbe() { RemoveWindowSubclass(window, proc, 91); }
    void reset() { paints = moves = captions = 0; }
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto &probe = *(NavigationPaintProbe *)data;
        if (msg == WM_PAINT) ++probe.paints;
        if (msg == WM_WINDOWPOSCHANGING) ++probe.moves;
        if (msg == WM_SETTEXT) ++probe.captions;
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
};
template<typename Check> void testNavigationDetails(App &app, Check require) {
    auto list = app.control(HostList), handle = app.control(NavigationResize);
    auto rectangle = [&](HWND window) {
        RECT r{}; GetWindowRect(window, &r); MapWindowPoints(nullptr, app.hwnd, (POINT *)&r, 2); return r;
    };
    auto pump = [](DWORD milliseconds) {
        auto end = GetTickCount64() + milliseconds;
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            Sleep(10);
        } while (GetTickCount64() < end);
    };
    auto dragTo = [&](int target) {
        POINT p{ui::px(target + app.navigationDragOffset), ui::px(220)};
        MapWindowPoints(app.hwnd, handle, &p, 1);
        SendMessageW(handle, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(p.x, p.y));
    };
    auto begin = [&] { SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ui::px(3), ui::px(220))); };
    auto end = [&] { SendMessageW(handle, WM_LBUTTONUP, 0, 0); };
    auto hover = [&](int index) {
        RECT r{}; require(SendMessageW(list, LB_GETITEMRECT, index, (LPARAM)&r) != LB_ERR, "Tooltip row exists");
        POINT p{ui::px(90), (r.top + r.bottom) / 2};
        POINT screen = p; ClientToScreen(list, &screen); SetCursorPos(screen.x, screen.y);
        SendMessageW(list, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
    };
    auto tooltip = [&] {
        NMTTDISPINFOW tip{}; tip.hdr = {app.tooltips, connectionTip, TTN_GETDISPINFOW};
        SendMessageW(list, WM_NOTIFY, connectionTip, (LPARAM)&tip);
        return tip.lpszText ? std::wstring(tip.lpszText) : L"";
    };
    Profile root; root.name = L"Local gateway"; root.host = L"127.0.0.1"; root.user = L"developer";
    Profile grouped = root; grouped.name = L"Saved production service";
    grouped.alias = L"운영 API · very long connection name for navigation tooltip";
    grouped.host = L"2001:db8::42"; grouped.port = 2222; grouped.group = L"Production · 운영 서버";
    app.profiles = {root, grouped}; app.folders = {{grouped.group}, {L"Empty folder"}};
    app.filter(L"host:" + grouped.name);
    app.navigationPreferredWidth = 252; wook::saveNavigationWidth(252); app.layout();
    RECT originalWindow{}; GetWindowRect(app.hwnd, &originalWindow);
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(1400), ui::px(820), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    auto selection = app.connectionSelection(); SetFocus(app.control(Quick));
    SetWindowTextW(app.control(Quick), L"keep-this-draft");
    auto checkGuide = [&](unsigned index, bool lastChild) {
        auto target = GetDC(list), memory = CreateCompatibleDC(target);
        auto bitmap = CreateCompatibleBitmap(target, ui::px(220), ui::px(65));
        auto old = SelectObject(memory, bitmap);
        DRAWITEMSTRUCT item{}; item.CtlType = ODT_LISTBOX; item.CtlID = HostList; item.itemID = index;
        item.itemAction = ODA_DRAWENTIRE; item.hwndItem = list; item.hDC = memory; item.rcItem = ui::rect(0, 0, 220, 65);
        SendMessageW(app.hwnd, WM_DRAWITEM, HostList, (LPARAM)&item);
        auto above = GetPixel(memory, ui::px(22), ui::px(10));
        auto elbow = GetPixel(memory, ui::px(28), ui::px(31));
        auto below = GetPixel(memory, ui::px(22), ui::px(50));
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(list, target);
        require(above == ui::line && elbow == ui::line, "Each folder child must connect to the tree above and to its terminal icon");
        require(below == (lastChild ? ui::panel : ui::line), "The last folder child must end its branch at the elbow, while earlier siblings continue");
    };
    checkGuide(2, true); // A single child, followed by another folder.
    auto sibling = grouped; sibling.name = L"Replica"; sibling.alias = L"Replica";
    app.profiles.push_back(sibling); app.filter();
    checkGuide(2, false); checkGuide(3, true);
    app.folders[0].collapsed = true; app.filter();
    require(app.connectionRows.size() == 3 && app.connectionRows[2].profile < 0, "Collapsed folders must not leave any child branches");
    SetWindowTextW(app.control(Search), grouped.name.c_str());
    require(app.connectionRows.size() == 2, "Search should expose only the matching child of the collapsed folder");
    checkGuide(1, true);
    SetWindowTextW(app.control(Search), L""); app.folders[0].collapsed = false; app.folders.pop_back(); app.filter();
    checkGuide(3, true); // Last child at the end of the entire connection list.
    for (int i = 0; i < 32; ++i) { auto extra = sibling; extra.name += std::to_wstring(i); app.profiles.push_back(extra); }
    app.filter();
    RECT listClient{}; GetClientRect(list, &listClient);
    auto bottom = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(ui::px(90), listClient.bottom - 1));
    require(LOWORD(bottom) + 1 < app.connectionRows.size(), "Tree fixture has siblings below the viewport");
    checkGuide(LOWORD(bottom), false);
    SendMessageW(list, LB_SETTOPINDEX, app.connectionRows.size() - 2, 0);
    checkGuide((unsigned)app.connectionRows.size() - 1, true);
    app.profiles.resize(2); app.folders.push_back({L"Empty folder"}); app.filter(selection);
    SendMessageW(list, LB_SETTOPINDEX, 0, 0);
    pump(100);
    NavigationPaintProbe probes[] {NavigationPaintProbe(app.control(NavigationToggle)), NavigationPaintProbe(app.control(NewFolder)),
        NavigationPaintProbe(app.control(MoveHostUp)), NavigationPaintProbe(app.control(MoveHostDown))};
    for (auto &probe : probes) {
        RECT r{}; GetClientRect(probe.window, &r);
        auto target = GetDC(probe.window), memory = CreateCompatibleDC(target);
        auto bitmap = CreateCompatibleBitmap(target, r.right, r.bottom);
        auto old = SelectObject(memory, bitmap);
        ui::fill(memory, r, RGB(91, 53, 117));
        SendMessageW(probe.window, WM_ERASEBKGND, (WPARAM)memory, 0);
        bool retained = GetPixel(memory, r.right / 2, r.bottom / 2) == RGB(91, 53, 117);
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(probe.window, target);
        require(retained, "Navigation button erasure must not expose an intermediate blank frame");
    }
    begin(); dragTo(420);
    for (auto &probe : probes) require(probe.paints == 0, "A resize gesture must finish positioning navigation buttons before painting any of them");
    pump(100);
    for (auto &probe : probes) require(probe.paints == 1, "Each moved navigation button should paint once per resize frame");
    require(app.navigationResizing && GetCapture() == handle && app.sidebar == 420, "Dragging the divider resizes immediately with capture");
    require(wook::loadNavigationWidth() == 252, "A drag does not write preferences on every mouse move");
    end();
    pump(100);
    for (auto &probe : probes) probe.reset();
    app.layout(); app.layout(); pump(100);
    for (auto &probe : probes) require(probe.paints == 0 && probe.moves == 0 && probe.captions == 0,
        "Unchanged layout and captions must not repaint or move navigation buttons");
    require(!app.navigationResizing && GetCapture() != handle && wook::loadNavigationWidth() == 420, "Releasing the divider saves its width");
    require(GetFocus() == app.control(Quick) && ui::value(app.control(Quick)) == L"keep-this-draft" && app.connectionSelection() == selection,
            "Dragging preserves input focus, draft and selected connection");
    require(rectangle(list).right <= rectangle(handle).left && rectangle(app.control(Search)).right < rectangle(handle).left,
            "List and search grow within the navigation divider");
    begin(); dragTo(300); SendMessageW(handle, WM_CANCELMODE, 0, 0);
    require(app.sidebar == 420 && wook::loadNavigationWidth() == 420 && !app.navigationResizing, "Canceled drag restores the saved width");
    begin(); dragTo(300); SetCapture(app.hwnd); ReleaseCapture();
    require(app.sidebar == 420 && !app.navigationResizing, "Losing capture cancels navigation resizing");
    app.setNavigation(false);
    require(app.sidebar == sidebarCollapsed && !IsWindowVisible(handle), "Collapsed navigation hides its divider");
    app.setNavigation(true);
    require(app.sidebar == 420, "Reopening navigation restores its chosen width");
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(980), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(app.width - app.sidebar >= wook::workspaceMinWidth && app.navigationPreferredWidth == 420 && wook::loadNavigationWidth() == 420,
            "Small windows retain workspace space without overwriting the preferred width");
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(1400), ui::px(820), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(app.sidebar == 420, "Expanding a window restores the preferred navigation width");
    begin(); dragTo(-1000); end();
    require(app.sidebar == 220, "Dragging outside the left edge clamps at the usable minimum");
    for (int id : {NewFolder, MoveHostUp, MoveHostDown, NavigationToggle}) {
        auto r = rectangle(app.control(id));
        require(r.left >= 0 && r.right <= rectangle(handle).left, "Navigation controls remain usable at minimum width");
    }
    begin(); dragTo(3000); end();
    require(app.sidebar == 520 && app.width - app.sidebar >= 700, "Dragging beyond the right edge remains bounded");
    SetFocus(handle); SendMessageW(handle, WM_KEYDOWN, VK_LEFT, 0);
    require(app.sidebar == 504 && wook::loadNavigationWidth() == 504, "Keyboard resizing uses the same persistence path");
    SendMessageW(handle, WM_KEYDOWN, VK_HOME, 0);
    require(app.sidebar == 252 && wook::loadNavigationWidth() == 252, "Home restores the default navigation width");
    SetFocus(app.control(Quick));
    begin(); dragTo(220); end(); pump(100);
    auto pixels = [&](HWND window) {
        RECT r{}; GetClientRect(window, &r); auto dc = GetDC(window);
        std::vector<COLORREF> result;
        for (int y = 2; y < r.bottom - 2; y += 2) for (int x = 2; x < r.right - 2; x += 2) result.push_back(GetPixel(dc, x, y));
        ReleaseDC(window, dc); return result;
    };
    for (int id : {HostList, NewFolder, MoveHostUp, MoveHostDown}) {
        auto control = app.control(id); auto beforePaint = pixels(control);
        RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        require(beforePaint == pixels(control), "Shrinking navigation must repaint clipped rows and moved buttons without a forced redraw");
    }
    SendMessageW(handle, WM_KEYDOWN, VK_HOME, 0);

    hover(1);
    require(tooltip().find(L"Folder: " + grouped.group) == 0 && tooltip().find(L"1 saved host") != std::wstring::npos,
            "Folder tooltip identifies the full name, type and host count");
    hover(2);
    auto text = tooltip();
    require(text.find(grouped.alias) != std::wstring::npos && text.find(grouped.name) != std::wstring::npos &&
            text.find(L"SSH terminal") != std::wstring::npos && text.find(L"developer@[2001:db8::42]:2222") != std::wstring::npos &&
            text.find(grouped.group) != std::wstring::npos, "Host tooltip contains full alias, saved name, endpoint and folder");
    auto before = app.connectionSelection(); auto focus = GetFocus();
    pump(900);
    require(IsWindowVisible(app.tooltips), "Pausing over a row shows a real native tooltip");
    require(app.connectionSelection() == before && GetFocus() == focus, "Hovering does not select a connection or steal focus");
    captureTestWindow(app.tooltips, L"ui-navigation-tooltip.bmp");
    SendMessageW(list, WM_MOUSELEAVE, 0, 0);
    require(app.connectionHover == -1 && !IsWindowVisible(app.tooltips), "Leaving a row dismisses its tooltip");
    hover(3); require(tooltip().find(L"0 saved hosts") != std::wstring::npos, "Empty folders have descriptive tooltips");
    SendMessageW(list, WM_MOUSEMOVE, 0, MAKELPARAM(ui::px(90), ui::px(400)));
    require(app.connectionHover == -1, "Blank space below the final row must not reuse its tooltip");
    hover(2); SendMessageW(list, WM_VSCROLL, SB_LINEDOWN, 0);
    require(app.connectionHover == -1 && !IsWindowVisible(app.tooltips), "Scrolling invalidates the old row tooltip");
    hover(2); SetWindowTextW(app.control(Search), L"not-a-fixture");
    require(app.connectionHover == -1 && tooltip().empty(), "Filtering invalidates stale row indices and tooltip text");
    SetWindowTextW(app.control(Search), L"");
    app.folders[0].collapsed = true; app.filter(); hover(1);
    require(tooltip().find(L"Collapsed") != std::wstring::npos, "Folder tooltip follows its collapse state");
    SetWindowTextW(app.control(Search), L"production"); hover(0);
    require(tooltip().find(L"matching search") != std::wstring::npos && tooltip().find(L"Expanded") != std::wstring::npos,
            "Search explains visible matching children without changing the saved collapse state");
    SetWindowTextW(app.control(Search), L""); app.folders[0].collapsed = false; app.filter();
    for (int i = 0; i < 70; ++i) { auto host = root; host.name = L"Extra " + std::to_wstring(i); app.profiles.push_back(host); }
    app.filter(); SendMessageW(list, LB_SETTOPINDEX, 40, 0); hover(40);
    require(tooltip().find(L"Extra 39") != std::wstring::npos, "A scrolled list resolves the row under the pointer, not its screen offset");
    app.profiles.resize(2); app.filter(L"host:" + grouped.name);
    SendMessageW(list, LB_SETTOPINDEX, 0, 0);

    // Preserve split identities, routing and command input while changing width.
    auto first = std::make_unique<Tab>(), second = std::make_unique<Tab>();
    first->preview = second->preview = true; first->profile = root; second->profile = grouped;
    app.tabs.push_back(std::move(first)); app.tabs.push_back(std::move(second)); app.active = 0; app.setSplit(2);
    auto panes = app.panes;
    SendMessageW(app.control(SendAll), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(app.control(SyncInput), BM_SETCHECK, BST_CHECKED, 0);
    SetWindowTextW(app.control(CommandText), L"preserve split draft"); SetFocus(app.control(CommandText));
    begin(); dragTo(360); end();
    require(app.panes == panes && app.tabs[0].get() == panes[0] && app.active == 0 &&
            ui::value(app.control(CommandText)) == L"preserve split draft" && GetFocus() == app.control(CommandText) &&
            SendMessageW(app.control(SendAll), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SendMessageW(app.control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED,
            "Resizing keeps split identities, active pane, input focus, draft and routing options");
    app.tabs.clear(); app.panes.clear(); app.active = -1; app.splitCount = 1; app.layout();
    app.navigationPreferredWidth = 360; app.layout(); captureTestWindow(app.hwnd, L"ui-navigation-wide.bmp", true);
    app.navigationPreferredWidth = 220; app.layout(); captureTestWindow(app.hwnd, L"ui-navigation-narrow.bmp", true);
    app.navigationPreferredWidth = 252; wook::saveNavigationWidth(252); app.layout();
    SetWindowPos(app.hwnd, nullptr, 0, 0, originalWindow.right - originalWindow.left, originalWindow.bottom - originalWindow.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
