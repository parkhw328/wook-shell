// Exercise Windows font enumeration/validation and persistent workspace chrome.
#include <dlgs.h>
bool fontPresentBeforeLoad = false;
bool testFontEnumerable() {
    LOGFONTW font{}; font.lfCharSet = DEFAULT_CHARSET; wcscpy_s(font.lfFaceName, L"JetBrains Mono");
    bool found = false; auto dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &font, [](const LOGFONTW *entry, const TEXTMETRICW *, DWORD, LPARAM data) -> int {
        if (std::wstring(entry->lfFaceName) == L"JetBrains Mono") *(bool *)data = true;
        return 1;
    }, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc); return found;
}
HWND fontTestOwner = nullptr;
int fontTestPoints = 0;
void CALLBACK testFontTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM timer) -> BOOL {
        if (GetWindow(hwnd, GW_OWNER) != fontTestOwner || !GetDlgItem(hwnd, cmb1) || !GetDlgItem(hwnd, cmb3)) return TRUE;
        KillTimer(nullptr, (UINT_PTR)timer);
        SetDlgItemTextW(hwnd, cmb3, std::to_wstring(fontTestPoints).c_str());
        captureTestWindow(hwnd, L"ui-font-picker.bmp");
        PostMessageW(hwnd, WM_COMMAND, IDOK, 0);
        return FALSE;
    }, (LPARAM)timer);
}
template<typename Check> void testBundledFonts(App &app, Check require) {
    require(testFontEnumerable(), "Bundled JetBrains Mono must be enumerable by the native font picker");
    auto root = wsRoot(); auto folder = std::filesystem::path(root) / L"fonts"; free(root);
    int fonts = 0;
    for (auto &entry : std::filesystem::directory_iterator(folder)) {
        require(entry.path().extension() == L".ttf", "The font cache must not leave temporary files");
        std::ifstream file(entry.path(), std::ios::binary);
        auto bytes = std::string(std::istreambuf_iterator<char>(file), {});
        require(bytes == wook::resourceBytes(entry.path().filename().wstring().find(L"Regular") != std::wstring::npos ? 103 : 104),
                "Cached fonts must exactly match the embedded licensed assets");
        ++fonts;
    }
    require(fonts == 2, "Regular and Bold must both be cached under isolated user data");
    unsigned weights = 0;
    LOGFONTW enumeration{}; enumeration.lfCharSet = DEFAULT_CHARSET; wcscpy_s(enumeration.lfFaceName, L"JetBrains Mono");
    auto dc = GetDC(app.hwnd);
    EnumFontFamiliesExW(dc, &enumeration, [](const LOGFONTW *font, const TEXTMETRICW *, DWORD, LPARAM data) -> int {
        if (font->lfWeight == FW_NORMAL) *(unsigned *)data |= 1;
        if (font->lfWeight == FW_BOLD) *(unsigned *)data |= 2;
        return 1;
    }, (LPARAM)&weights, 0);
    ReleaseDC(app.hwnd, dc);
    require(weights == 3, "Both bundled Regular and Bold must be available to Windows text rendering");
    LOGFONTW font{}; wcscpy_s(font.lfFaceName, L"JetBrains Mono");
    font.lfCharSet = DEFAULT_CHARSET; font.lfHeight = -MulDiv(9, GetDpiForWindow(app.hwnd), 72);
    for (int points : {13, 9}) {
        font.lfWeight = FW_NORMAL;
        CHOOSEFONTW picker{sizeof(picker)}; picker.hwndOwner = app.hwnd; picker.lpLogFont = &font;
        picker.Flags = CF_FIXEDPITCHONLY | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;
        // A CF_ENABLEHOOK would switch ChooseFont to its legacy template.
        // Drive the actual unmodified Windows dialog used by terminal settings.
        fontTestOwner = app.hwnd; fontTestPoints = points;
        auto timer = SetTimer(nullptr, 0, 150, testFontTimer);
        auto accepted = ChooseFontW(&picker); KillTimer(nullptr, timer);
        require(accepted != FALSE, "Native font picker must accept bundled fonts without a system install");
        require(picker.iPointSize == points * 10 && std::wstring(font.lfFaceName) == L"JetBrains Mono",
                "Native font picker must allow changing to 13pt and back to 9pt");
    }
    std::ofstream report(std::filesystem::path(app.directory) / L"font-picker.json");
    report << "{\"passed\":true,\"fontPresentBeforeLoad\":" << (fontPresentBeforeLoad ? "true" : "false") << ",\"sizes\":[13,9]}\n";
}
template<typename Check> void testMenuStyle(App &app, Check require) {
    auto menu = GetMenu(app.hwnd);
    auto originalDpi = ui::dpi;
    auto dc = GetDC(app.hwnd), memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, 800, 160);
    auto oldBitmap = SelectObject(memory, bitmap);
    for (unsigned dpi : {96u, 144u, 192u}) {
        ui::dpi = dpi;
        for (int group = 0; group < GetMenuItemCount(menu); ++group) {
            MENUITEMINFOW heading{sizeof(heading)}; heading.fMask = MIIM_DATA | MIIM_SUBMENU;
            require(GetMenuItemInfoW(menu, group, TRUE, &heading) != FALSE, "Cannot inspect menu heading");
            MEASUREITEMSTRUCT size{}; size.CtlType = ODT_MENU; size.itemData = heading.dwItemData;
            require(app.menuBar.measure(size), "Menu must measure each native heading");
            auto name = (MSAAMENUINFO *)heading.dwItemData;
            auto oldFont = SelectObject(memory, ui::font(ui::TextSize::caption));
            SIZE text{}; GetTextExtentPoint32W(memory, name->pszWText, name->cchWText, &text);
            SelectObject(memory, oldFont);
            require(size.itemWidth >= text.cx + ui::px(32), "Headings need at least 16 logical pixels on each side");
            for (int index = 0; index < GetMenuItemCount(heading.hSubMenu); ++index) {
                MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_FTYPE | MIIM_DATA;
                GetMenuItemInfoW(heading.hSubMenu, index, TRUE, &item);
                size = {}; size.CtlType = ODT_MENU; size.itemData = item.dwItemData;
                require(app.menuBar.measure(size), "Menu must measure commands and separators");
                if (!(item.fType & MFT_SEPARATOR)) {
                    require(size.itemHeight >= (UINT)ui::px(38), "Popup commands must have comfortable row spacing");
                    continue;
                }
                require(item.fType & MFT_OWNERDRAW, "Separators must bypass the native etched white line");
                DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_MENU; draw.itemData = item.dwItemData;
                draw.hDC = memory; draw.rcItem = {0, 0, ui::px(300), (LONG)size.itemHeight};
                require(app.menuBar.draw(draw), "Menu must render its separators");
                int center = size.itemHeight / 2;
                require(GetPixel(memory, ui::px(150), center) == ui::line &&
                        GetPixel(memory, ui::px(150), center - 1) == ui::panel &&
                        GetPixel(memory, ui::px(150), center + 1) == ui::panel &&
                        GetPixel(memory, ui::px(8), center) == ui::panel,
                        "Separators must be one subdued pixel with inset ends at every DPI");
            }
        }
    }
    ui::dpi = originalDpi;
    SelectObject(memory, oldBitmap); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(app.hwnd, dc);
    for (UINT message : {WM_NCPAINT, WM_NCACTIVATE}) {
        SendMessageW(app.hwnd, message, TRUE, 0);
        RECT window{}; GetWindowRect(app.hwnd, &window);
        POINT client{0, 0}; ClientToScreen(app.hwnd, &client);
        dc = GetWindowDC(app.hwnd);
        auto color = GetPixel(dc, client.x - window.left + ui::px(400), client.y - window.top - 1);
        MENUBARINFO bar{sizeof(bar)};
        require(GetMenuBarInfo(app.hwnd, OBJID_MENU, 0, &bar), "Native menu geometry must stay available");
        auto unused = GetPixel(dc, bar.rcBar.right - window.left - 2,
            (bar.rcBar.top + bar.rcBar.bottom) / 2 - window.top);
        auto top = GetPixel(dc, bar.rcBar.left - window.left + ui::px(400), bar.rcBar.top - window.top - 1);
        ReleaseDC(app.hwnd, dc);
        require(color == ui::line, "The menu bar edge must stay subdued after frame painting and activation");
        require(unused == ui::panel && top == ui::line, "Unused menu space and the upper edge must not retain system white");
    }
}
struct MenuPopupCheck { HMENU menu; bool found = false, dark = false; };
MenuPopupCheck *menuPopupCheck = nullptr;
template<typename Check> void testMenuPopupBorder(App &app, Check require) {
    MenuPopupCheck check{GetSubMenu(app.menuBar.handle(), 1)};
    menuPopupCheck = &check;
    auto timer = SetTimer(nullptr, 0, 250, [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
            MENUBARINFO info{sizeof(info)};
            if (!GetMenuBarInfo(window, OBJID_CLIENT, 0, &info) || info.hMenu != menuPopupCheck->menu) return TRUE;
            menuPopupCheck->found = true;
            RECT rect{}; GetWindowRect(window, &rect);
            int width = rect.right - rect.left, height = rect.bottom - rect.top;
            auto dc = GetWindowDC(window);
            menuPopupCheck->dark = GetPixel(dc, width / 2, 0) == ui::line &&
                GetPixel(dc, width / 2, height - 1) == ui::line &&
                GetPixel(dc, 0, height / 2) == ui::line && GetPixel(dc, width - 1, height / 2) == ui::line;
            ReleaseDC(window, dc);
            return FALSE;
        }, 0);
        EndMenu();
    });
    require(timer != 0, "Cannot start popup appearance check");
    RECT heading{}; GetMenuItemRect(app.hwnd, app.menuBar.handle(), 1, &heading);
    TrackPopupMenuEx(check.menu, TPM_LEFTALIGN | TPM_TOPALIGN, heading.left, heading.bottom, app.hwnd, nullptr);
    KillTimer(nullptr, timer); menuPopupCheck = nullptr;
    require(check.found && check.dark, "The actual popup must have a subdued border on all four sides");
}
template<typename Check> void testWorkspaceChrome(App &app, Check require) {
    testMenuStyle(app, require);
    auto rectangle = [&](HWND control) { RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, app.hwnd, (POINT *)&r, 2); return r; };
    auto menu = GetMenu(app.hwnd);
    require(menu && menu == app.menuBar.handle() && GetMenuItemCount(menu) == 6,
            "Workspace must expose a native six-section menu bar");
    for (int id : {NewHost, ConnectHost, FilesHost, EditHost, Advanced, Tools, Shortcuts, About})
        require(!app.control(id), "Top-level actions must be menu items, not duplicated buttons");
    RECT window{}; GetWindowRect(app.hwnd, &window);
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(980), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT minimum{}; GetWindowRect(app.hwnd, &minimum);
    const wchar_t *names[] = {L"File", L"Host", L"Session", L"View", L"Tools", L"Help"};
    int right = minimum.left;
    for (int i = 0; i < 6; ++i) {
        RECT r{};
        require(GetMenuItemRect(app.hwnd, menu, i, &r) && r.left >= right && r.right <= minimum.right &&
                r.top >= minimum.top, "Native menu headings must fit without overlap at minimum size");
        right = r.right;
        MENUITEMINFOW info{sizeof(info)}; info.fMask = MIIM_DATA | MIIM_SUBMENU;
        require(GetMenuItemInfoW(menu, i, TRUE, &info) && info.hSubMenu && info.dwItemData,
                "Each heading must open a native submenu");
        auto accessible = (MSAAMENUINFO *)info.dwItemData;
        require(accessible->dwMSAASignature == MSAA_MENU_SIG && std::wstring(accessible->pszWText) == names[i],
                "Owner-drawn menu headings must expose readable accessibility names");
    }
    auto hostMenu = GetSubMenu(menu, 1);
    auto mnemonic = SendMessageW(app.hwnd, WM_MENUCHAR, MAKEWPARAM('e', MF_POPUP), (LPARAM)hostMenu);
    require(HIWORD(mnemonic) == MNC_EXECUTE && GetMenuItemID(hostMenu, LOWORD(mnemonic)) == EditHost,
            "Host menu mnemonic must resolve to the existing edit command");
    require(!app.menuBar.enabled(MenuCloseTab) && !app.menuBar.enabled(FilesSession) && !app.menuBar.enabled(SessionSettings),
            "Home must disable commands that require an active session");
    auto quick = rectangle(app.control(Quick));
    auto toggle = rectangle(app.control(NavigationToggle));
    require(toggle.top >= 0 && toggle.right < ui::px(sidebarExpanded) &&
            toggle.bottom < rectangle(app.control(Search)).top, "Hide hosts must be beside Connections in the navigation header");
    require(app.navigationVisible && IsWindowVisible(app.control(Search)), "Navigation must start expanded");
    SetFocus(app.control(NavigationToggle)); SendMessageW(app.control(NavigationToggle), BM_CLICK, 0, 0);
    require(!app.navigationVisible && !wook::loadNavigationVisible() && !IsWindowVisible(app.control(Search)) && !IsWindowVisible(app.control(HostList)),
            "Hiding navigation must persist and remove its controls");
    require(GetFocus() == app.control(Quick) && rectangle(app.control(Quick)).left < quick.left,
            "Hiding navigation on the home screen must reclaim space and keep input usable");
    require(GetMenu(app.hwnd) == menu && GetMenuItemCount(menu) == 6 &&
            !(GetMenuState(menu, NavigationToggle, MF_BYCOMMAND) & MF_CHECKED),
            "Hiding navigation must preserve the menu bar and clear its checked state");
    toggle = rectangle(app.control(NavigationToggle));
    require(IsWindowVisible(app.control(NavigationToggle)) && toggle.left >= 0 && toggle.right <= ui::px(app.sidebar) &&
            toggle.top >= 0 && ui::value(app.control(NavigationToggle)) == L"Show hosts",
            "Collapsed navigation must keep an accessible restore button in its own rail");
    captureTestWindow(app.hwnd, L"ui-navigation-collapsed.bmp");
    app.command(7);
    require(app.navigationVisible && wook::loadNavigationVisible() && GetFocus() == app.control(Search),
            "Search shortcut must reopen navigation and focus search");
    wchar_t cue[100]{}; SendMessageW(app.control(Search), EM_GETCUEBANNER, (WPARAM)cue, 100);
    require(std::wstring(cue) == L"Name, alias, address…", "Search must describe names and addresses");
    auto profile = app.profiles[0];
    app.profiles[0].alias = L"QA display alias";
    for (auto query : {profile.name, std::wstring(L"DISPLAY ALIAS"), profile.host, profile.group, profile.user}) {
        SetWindowTextW(app.control(Search), query.c_str());
        require(app.filtered.size() == 1 && app.selectedHost()->name == profile.name, "Search must match name, alias, address, group and user");
    }
    SetWindowTextW(app.control(Search), L"missing-connection-fixture");
    require(app.filtered.empty() && !app.menuBar.enabled(ConnectHost) && !app.menuBar.enabled(EditHost) &&
            !app.menuBar.enabled(FilesHost), "Empty search must disable actions requiring a saved connection");
    auto ignored = SendMessageW(app.hwnd, WM_MENUCHAR, MAKEWPARAM('c', MF_POPUP), (LPARAM)hostMenu);
    require(HIWORD(ignored) == MNC_IGNORE, "Disabled menu commands must not execute through mnemonics");
    SendMessageW(app.hwnd, WM_COMMAND, FilesHost, 0);
    require(app.tabs.empty(), "Disabled SFTP menu dispatch must not start a connection");
    app.setNavigation(false); SetWindowTextW(app.control(Search), L"");
    require(!IsWindowVisible(app.control(HostList)), "Refreshing a hidden navigation list must not show it");
    app.setNavigation(true); app.profiles[0] = profile; app.filter();
    require(app.menuBar.enabled(ConnectHost) && app.menuBar.enabled(EditHost) && app.menuBar.enabled(FilesHost),
            "Clearing search must restore saved SSH host actions");
    app.profiles[0].protocol = L"serial"; app.filter();
    require(!app.menuBar.enabled(FilesHost), "SFTP must stay disabled for a serial host");
    app.profiles[0] = profile; app.filter();
    captureTestWindow(app.hwnd, L"ui-navigation-minimum.bmp");
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetFocus(app.control(Quick)); captureTestWindow(app.hwnd, L"ui-navigation-expanded.bmp");
}
