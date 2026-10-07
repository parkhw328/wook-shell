template<typename Check> void testTabOverflow(App &app, Check require) {
    require(app.tabs.empty(), "Tab overflow fixture requires an empty workspace");
    RECT window{}; GetWindowRect(app.hwnd, &window);
    for (int i = 0; i < 32; ++i) {
        auto tab = std::make_unique<Tab>(); tab->ended = true;
        tab->profile.name = L"Server " + std::to_wstring(i + 1) + L" · 서울 workspace";
        tab->profile.host = L"192.0.2." + std::to_wstring(i + 1); app.tabs.push_back(std::move(tab));
    }
    for (int width : {980, 1400}) {
        SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(width), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        app.select(31); UpdateWindow(app.hwnd);
        require(app.strip.overflow && IsWindowVisible(app.control(TabsPrevious)) && IsWindowVisible(app.control(TabsNext)) &&
                IsWindowVisible(app.control(TabsList)), "Hidden tabs must have visible arrows and an all-tabs menu at small and large sizes");
        require(!IsWindowEnabled(app.control(TabsNext)) && IsWindowEnabled(app.control(TabsPrevious)), "Last page enables only the usable scroll direction");
        require(std::any_of(app.hits.begin(), app.hits.end(), [](const auto &hit) { return hit.index == 31; }), "Newly selected last tab must be visible");
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
    app.tabs.clear(); app.select(-1);
    require(!IsWindowVisible(app.control(TabsList)) && app.tabScroll == 0, "Closing all tabs resets the strip");
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
