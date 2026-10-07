struct CloseTabsTest {
    App *app{};
    int answer{}, prompts{};
    bool mutate{}, remember{}, settings{}, defaultFocused = true, initialChecked{};
    uint64_t newTab{};
};
CloseTabsTest *closeTabsTest = nullptr;
void CALLBACK closeTabsTestTimer(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        auto &test = *closeTabsTest;
        if (GetWindow(window, GW_OWNER) != test.app->hwnd) return TRUE;
        auto title = ui::value(window);
        if (test.settings ? title != L"wShell · Application settings" : title != L"Close tabs" && title != L"Close tab") return TRUE;
        test.defaultFocused &= GetFocus() == GetDlgItem(window, IDOK);
        test.initialChecked = SendDlgItemMessageW(window, 1001, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (test.remember) SendDlgItemMessageW(window, 1001, BM_SETCHECK, BST_CHECKED, 0);
        captureTestWindow(window, test.settings ? L"ui-application-settings.bmp" : L"ui-close-confirm.bmp");
        if (++test.prompts == 1 && test.mutate) {
            test.app->closeTab(1, true);
            auto tab = std::make_unique<Tab>(); tab->ended = true; tab->profile.name = L"Opened during confirmation";
            test.newTab = tab->id; test.app->tabs.push_back(std::move(tab)); test.app->layout();
        }
        PostMessageW(window, WM_COMMAND, test.answer, 0); return FALSE;
    }, 0);
}
template<typename Check> void testBulkTabConsent(App &app, Check require) {
    require(app.tabs.empty(), "Bulk close fixture starts without live user sessions");
    for (int i = 0; i < 4; ++i) {
        auto tab = std::make_unique<Tab>(); tab->files = true; tab->profile.name = L"Close confirmation fixture";
        app.tabs.push_back(std::move(tab));
    }
    app.select(2); auto anchor = app.tabs[2]->id; app.setSplit(4);
    SetWindowTextW(app.control(CommandText), L"draft must survive cancellation");
    CloseTabsTest test{&app, IDCANCEL}; test.remember = true; closeTabsTest = &test;
    auto timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    app.closeTabs(CloseTabs::others, anchor); KillTimer(nullptr, timer);
    require(test.prompts == 1 && app.tabs.size() == 4 && app.tabs[app.active]->id == anchor && app.panes.size() == 4 &&
            ui::value(app.control(CommandText)) == L"draft must survive cancellation", "Cancel must preserve every tab, active pane and input draft");
    require(wook::loadConfirmCloseTabs(), "Checking do not ask again and cancelling must not change the preference");
    test.answer = IDOK; test.prompts = 0;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    app.closeTabs(CloseTabs::others, anchor); KillTimer(nullptr, timer);
    require(test.prompts == 1 && app.tabs.size() == 1 && app.tabs[app.active]->id == anchor,
            "Closing multiple active tabs must require exactly one confirmation and preserve the chosen tab");
    require(!wook::loadConfirmCloseTabs(), "Accepting do not ask again must remember the choice");
    test.settings = true; test.answer = IDCANCEL; test.prompts = 0;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    showApplicationSettings(app.hwnd); KillTimer(nullptr, timer);
    require(test.prompts == 1 && !test.initialChecked && !wook::loadConfirmCloseTabs(), "Cancelling settings must preserve disabled close prompts");
    test.answer = IDOK; test.prompts = 0;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    showApplicationSettings(app.hwnd); KillTimer(nullptr, timer);
    require(test.prompts == 1 && wook::loadConfirmCloseTabs(), "Application settings must restore close confirmations");
    test.settings = false; test.remember = false;
    for (int i = 0; i < 3; ++i) { auto tab = std::make_unique<Tab>(); tab->files = true; app.tabs.push_back(std::move(tab)); }
    test.prompts = 0; test.mutate = true;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    app.closeTabs(CloseTabs::all); KillTimer(nullptr, timer);
    require(test.prompts == 1 && app.tabs.size() == 1 && app.tabs[0]->id == test.newTab,
            "A nested message loop must not close a newly opened tab after an original target disappears");
    app.closeTabs(CloseTabs::all); SetWindowTextW(app.control(CommandText), L"");
    for (int i = 0; i < 2; ++i) { auto tab = std::make_unique<Tab>(); tab->files = true; app.tabs.push_back(std::move(tab)); }
    app.select(0); test.mutate = false; test.remember = true; test.prompts = 0;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    app.closeTab(0); KillTimer(nullptr, timer);
    require(test.prompts == 1 && app.tabs.size() == 1 && !wook::loadConfirmCloseTabs(), "Single tab closing must also remember do not ask again");
    test.prompts = 0; test.answer = IDCANCEL;
    timer = SetTimer(nullptr, 0, 80, closeTabsTestTimer);
    app.closeTabs(CloseTabs::all); KillTimer(nullptr, timer);
    require(test.prompts == 0 && app.tabs.empty(), "Remembered choice must skip the next close confirmation");
    require(test.defaultFocused, "Close and Save must receive the initial keyboard focus");
    wook::saveConfirmCloseTabs(true); closeTabsTest = nullptr;
}
template<typename Check> void testTabOverflow(App &app, Check require) {
    require(app.tabs.empty(), "Tab overflow fixture requires an empty workspace");
    RECT window{}; GetWindowRect(app.hwnd, &window);
    for (int i = 0; i < 32; ++i) {
        auto tab = std::make_unique<Tab>(); tab->ended = true;
        tab->profile.name = L"Server " + std::to_wstring(i + 1) + L" · 서울 workspace";
        const wchar_t *colors[] = {L"d14d41", L"4385be", L"879a39", L"8b7ec8"};
        tab->profile.tabColor = colors[i % 4];
        tab->profile.host = L"192.0.2." + std::to_wstring(i + 1); app.tabs.push_back(std::move(tab));
    }
    for (int width : {980, 1400}) {
        SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(width), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        app.select(31); UpdateWindow(app.hwnd);
        require(app.strip.overflow && IsWindowVisible(app.control(TabsPrevious)) && IsWindowVisible(app.control(TabsNext)) &&
                IsWindowVisible(app.control(TabsList)), "Hidden tabs must have visible arrows and an all-tabs menu at small and large sizes");
        require(!IsWindowEnabled(app.control(TabsNext)) && IsWindowEnabled(app.control(TabsPrevious)), "Last page enables only the usable scroll direction");
        require(std::any_of(app.hits.begin(), app.hits.end(), [](const auto &hit) { return hit.index == 31; }), "Newly selected last tab must be visible");
        // Rendering checks do not depend on another app obscuring the desktop.
        RECT client{}; GetClientRect(app.hwnd, &client);
        auto screen = GetDC(app.hwnd), dc = CreateCompatibleDC(screen);
        auto bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
        auto oldBitmap = SelectObject(dc, bitmap);
        app.paint(dc);
        bool hueVisible = true, selectionVisible = true;
        for (auto &hit : app.hits) if (!hit.close) {
            bool colored = !app.tabs[hit.index]->profile.tabColor.empty();
            auto got = GetPixel(dc, hit.rect.left + ui::px(colored ? 20 : 6), hit.rect.top + ui::px(colored ? 4 : 18));
            hueVisible &= got == hostColor(app.tabs[hit.index]->profile);
            if (hit.index == app.active)
                selectionVisible &= GetPixel(dc, hit.rect.left + ui::px(20), hit.rect.bottom - ui::px(2)) == ui::accent;
        }
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(app.hwnd, screen);
        require(hueVisible, "Visible tabs must show their assigned color band or default accent");
        require(selectionVisible, "Active tab selection must remain orange independently of its host color");
        auto panes = app.panes;
        for (int i = 0; i < 40; ++i) SendMessageW(app.control(TabsPrevious), BM_CLICK, 0, 0);
        require(app.tabScroll == 0 && app.active == 31 && app.panes == panes, "Scrolling the strip must not replace the active connection");
        app.poll(); app.layout();
        require(app.tabScroll == 0, "Background layout and polling must not snap a manually scrolled strip back to the active tab");
        require(!IsWindowEnabled(app.control(TabsPrevious)) && IsWindowEnabled(app.control(TabsNext)), "First page bounds the scroll buttons");
        POINT pointer{ui::px(app.sidebar + 150), ui::px(appBarHeight + 25)}; ClientToScreen(app.hwnd, &pointer);
        SendMessageW(app.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -60), MAKELPARAM(pointer.x, pointer.y));
        require(app.tabScroll == 0, "High resolution wheels accumulate partial steps");
        SendMessageW(app.hwnd, WM_APP + 48, MAKEWPARAM(0, -60), MAKELPARAM(pointer.x, pointer.y));
        require(app.tabScroll == 1 && app.active == 31, "Focused terminal wheel forwarding scrolls tabs without changing selection");
        SendMessageW(app.hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, 120), MAKELPARAM(pointer.x, pointer.y));
        require(app.tabScroll == 2, "Horizontal wheel scrolls right");
        captureTestWindow(app.hwnd, width == 980 ? L"ui-tabs-overflow-small.bmp" : L"ui-tabs-overflow-large.bmp");
        // Native mouse clicks focus buttons; reaching the edge disables the
        // button during BN_CLICKED. Restore the editor even at that boundary.
        if (GetForegroundWindow() == app.hwnd) {
            app.scrollTabs(1 - app.tabScroll); SetFocus(app.control(Search));
            auto button = app.control(TabsPrevious);
            SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
            require(app.tabScroll == 0 && !IsWindowEnabled(button) && GetFocus() == app.control(Search),
                    "A mouse scroll reaching the edge must restore the prior editor focus");
        }
        app.command(6); UpdateWindow(app.hwnd);
        require(app.active == 30 && std::any_of(app.hits.begin(), app.hits.end(), [](const auto &hit) { return hit.index == 30; }), "Ctrl+Shift+Tab reveals the selected offscreen tab");
        auto selectedName = app.tabs[app.active]->profile.name;
        app.closeTab(0);
        require(app.active == 29 && app.tabs[app.active]->profile.name == selectedName, "Closing an earlier tab preserves active identity");
        auto replacement = std::make_unique<Tab>(); replacement->ended = true; replacement->profile.name = L"Replacement"; app.tabs.push_back(std::move(replacement));
    }
    app.tabs.resize(4); app.select(0);
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(1400), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    require(!app.strip.overflow && app.strip.width >= 112 && !IsWindowVisible(app.control(TabsPrevious)), "Tabs compress before showing overflow controls");
    app.setSplit(4);
    auto keep = app.tabs[2]->id;
    app.closeTabs(CloseTabs::others, keep);
    require(app.tabs.size() == 1 && app.tabs[app.active]->id == keep && app.splitCount == 1 && app.panes.size() == 1,
            "Close other tabs must preserve the selected identity and leave a single pane");
    for (int i = 0; i < 4; ++i) { auto tab = std::make_unique<Tab>(); tab->ended = true; app.tabs.push_back(std::move(tab)); }
    auto reference = app.tabs[2]->id;
    app.select(2); app.closeTabs(CloseTabs::right, reference);
    require(app.tabs.size() == 3 && app.tabs[app.active]->id == reference && app.tabs.front()->id == keep,
            "Close tabs to the right must retain the reference and all earlier tabs");
    app.closeTab(app.active);
    app.closeTabs(CloseTabs::others, reference);
    require(app.tabs.size() == 2, "A disappeared menu anchor must not close unrelated tabs");
    app.closeTabs(CloseTabs::all);
    require(app.tabs.empty() && app.active == -1 && app.panes.empty(), "Close all tabs must return to the empty workspace");
    require(!IsWindowVisible(app.control(TabsList)) && app.tabScroll == 0, "Closing all tabs resets the strip");
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    testBulkTabConsent(app, require);
}
