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
template<typename Check> void testWorkspaceChrome(App &app, Check require) {
    auto rectangle = [&](HWND control) { RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, app.hwnd, (POINT *)&r, 2); return r; };
    const int actions[] = {NavigationToggle, NewHost, ConnectHost, FilesHost, EditHost, Advanced, Tools, About};
    RECT window{}; GetWindowRect(app.hwnd, &window);
    SetWindowPos(app.hwnd, nullptr, 0, 0, ui::px(980), ui::px(740), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    int right = 0;
    for (auto id : actions) {
        auto r = rectangle(app.control(id));
        require(IsWindowVisible(app.control(id)) && r.top >= 0 && r.bottom < ui::px(appBarHeight) &&
                r.left >= right && r.right <= ui::px(app.width), "Top actions must remain visible and non-overlapping at minimum size");
        right = r.right;
    }
    auto quick = rectangle(app.control(Quick));
    require(app.navigationVisible && IsWindowVisible(app.control(Search)), "Navigation must start expanded");
    SetFocus(app.control(NavigationToggle)); SendMessageW(app.control(NavigationToggle), BM_CLICK, 0, 0);
    require(!app.navigationVisible && !wook::loadNavigationVisible() && !IsWindowVisible(app.control(Search)) && !IsWindowVisible(app.control(HostList)),
            "Hiding navigation must persist and remove its controls");
    require(GetFocus() == app.control(Quick) && rectangle(app.control(Quick)).left < quick.left,
            "Hiding navigation on the home screen must reclaim space and keep input usable");
    for (auto id : actions) require(IsWindowVisible(app.control(id)), "Navigation toggle must not hide top actions");
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
    require(app.filtered.empty() && !IsWindowEnabled(app.control(ConnectHost)) && !IsWindowEnabled(app.control(EditHost)) &&
            !IsWindowEnabled(app.control(FilesHost)), "Empty search must disable actions requiring a saved connection");
    app.setNavigation(false); SetWindowTextW(app.control(Search), L"");
    require(!IsWindowVisible(app.control(HostList)), "Refreshing a hidden navigation list must not show it");
    app.setNavigation(true); app.profiles[0] = profile; app.filter();
    require(IsWindowEnabled(app.control(ConnectHost)) && IsWindowEnabled(app.control(EditHost)) && IsWindowEnabled(app.control(FilesHost)),
            "Clearing search must restore saved SSH host actions");
    app.profiles[0].protocol = L"serial"; app.filter();
    require(!IsWindowEnabled(app.control(FilesHost)), "SFTP must stay disabled for a serial host");
    app.profiles[0] = profile; app.filter();
    captureTestWindow(app.hwnd, L"ui-navigation-minimum.bmp");
    SetWindowPos(app.hwnd, nullptr, 0, 0, window.right - window.left, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetFocus(app.control(Quick)); captureTestWindow(app.hwnd, L"ui-navigation-expanded.bmp");
}
