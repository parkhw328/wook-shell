// Executes against the actual generated controls, only in the UI test binary.
void runSettingsSmoke(Settings *s) {
    auto check = [&](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); ++s->testChecks; };
    auto visit = [&](const char *path) {
        for (int i = 0; i < (int)s->pages.size(); ++i) if (s->pages[i].path == path) { choose(s, i); return; }
        throw std::runtime_error("Required settings page is missing");
    };
    auto input = [&](const wchar_t *label) -> HWND {
        for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
            wchar_t kind[32]{}; GetClassNameW(control, kind, 32);
            if (!wcscmp(kind, L"Static") && clean(ui::value(control)).find(label) != std::wstring::npos)
                return GetDlgItem(s->page, GetDlgCtrlID(control) + 1);
        }
        throw std::runtime_error("Settings input label was not found");
    };
    auto reportName = s->authorities ? L"settings-authorities.json" : s->live ? L"settings-live-" + std::to_wstring(s->testRun) + L".json" : L"settings-connect.json";
    auto report = [&](bool passed, const char *error = "") {
        std::ofstream output(std::filesystem::path(wook::executableDirectory()) / reportName);
        output << "{\"passed\":" << (passed ? "true" : "false") << ",\"checks\":" << s->testChecks
               << ",\"pages\":" << s->pages.size() << ",\"error\":\"" << error << "\"}\n";
    };
    try {
        if (s->authorities) {
            KillTimer(s->window, 92);
            check(s->pages.size() == 1, "Host authorities page is missing");
            check(ui::value(s->accept) == L"Done" && !IsWindowVisible(s->cancel), "Host authorities must expose the correct close action");
            SetWindowTextW(input(L"Name for this CA"), L"UI test authority");
            std::ifstream in(std::filesystem::path(wook::executableDirectory()) / L"client-key.pub");
            std::string type, blob; in >> type >> blob;
            check(!blob.empty(), "Host authority public key fixture is missing");
            SetWindowTextW(input(L"Public key of certification authority"), wook::wide(blob).c_str());
            SetWindowTextW(input(L"Valid hosts"), L"*.example.invalid");
            bool saved = false;
            for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT))
                if (clean(ui::value(control)) == L"Save") { SendMessageW(control, BM_CLICK, 0, 0); saved = true; break; }
            check(saved, "Host authority save action is missing");
            wchar_t *path = wsPath(L"cas", "UI test authority"); auto store = wsOpen(path, 0); free(path);
            check(store && store->exists, "Host authority must save through the native settings controls"); wsClose(store);
            captureTestWindow(s->window, L"ui-settings-authorities.bmp");
            report(true); s->submit(s->context, 1); return;
        }
        if (!s->live && s->testStep < (int)s->pages.size()) {
            choose(s, s->testStep++);
            auto &page = s->pages[s->selected];
            // A stale page can leave pixels in the gaps between its controls.
            // A normal paint (without WM_ERASEBKGND) must clear those pixels.
            RedrawWindow(s->page, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            auto dc = GetDC(s->page); SetPixel(dc, 1, 1, RGB(255, 0, 255)); ReleaseDC(s->page, dc);
            InvalidateRect(s->page, nullptr, FALSE); UpdateWindow(s->page);
            dc = GetDC(s->page); auto gap = GetPixel(dc, 1, 1); ReleaseDC(s->page, dc);
            check(gap == ui::bg, "Repainting a settings page must clear previous-page remnants without requiring background erasure");
            int controls = 0;
            for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
                LOGFONTW font{}; GetObjectW((HFONT)SendMessageW(control, WM_GETFONT, 0, 0), sizeof(font), &font);
                check(std::wstring(font.lfFaceName) == L"JetBrains Mono", "Every settings control must use the shared font");
                RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, s->page, (POINT *)&r, 2);
                check(r.left >= 0 && r.right <= client(s->page).right + 2, "Settings control exceeds the page width");
                ++controls;
            }
            check(controls > 0, "Settings page has no controls");
            // Measure real control rectangles; ignore enclosing group boxes.
            std::vector<RECT> fields;
            for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
                wchar_t kind[32]{}; GetClassNameW(control, kind, 32);
                if (!IsWindowVisible(control) || (!wcscmp(kind, L"Button") &&
                    (GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)) continue;
                RECT r{}; GetWindowRect(control, &r);
                for (auto other : fields) { RECT overlap{}; check(!IntersectRect(&overlap, &r, &other), "Settings controls overlap"); }
                fields.push_back(r);
                if (!wcscmp(kind, L"ComboBox") && (GetWindowLongPtrW(control, GWL_STYLE) & 3) == CBS_DROPDOWN) {
                    COMBOBOXINFO combo{sizeof(combo)};
                    if (GetComboBoxInfo(control, &combo) && IsWindowVisible(combo.hwndItem))
                        check(testRepaintPreservesControl(control, combo.hwndItem), "Combo redraw must preserve its editable text");
                }
            }
            for (const auto &[path, file] : std::vector<std::pair<std::string, std::wstring>>{
                {"Session", L"ui-settings-connection.bmp"}, {"Connection/Proxy", L"ui-settings-proxy.bmp"},
                {"Connection/SSH/Tunnels", L"ui-settings-tunnels.bmp"}, {"Terminal/Keyboard", L"ui-settings-keyboard.bmp"},
                {"Window/Colours", L"ui-settings-colors.bmp"}})
                if (page.path == path) captureTestWindow(s->window, file.c_str());
            return;
        }
        KillTimer(s->window, 92);
        if (!s->live) {
            check(s->pages.size() >= 25, "Advanced settings pages were lost");
            SetWindowTextW(s->search, L"keepalive");
            check(!s->filtered.empty() && s->filtered.size() < s->pages.size(), "Settings search must match field labels");
            SetWindowTextW(s->search, L"no-such-setting-123456");
            check(s->filtered.empty(), "Settings search must handle no results");
            SetWindowTextW(s->search, L"");
            visit("Connection"); SetWindowTextW(input(L"Seconds between keepalives"), L"45");
            visit("Window"); SetWindowTextW(input(L"Lines of scrollback"), L"12000");
            visit("Connection");
            check(ui::value(input(L"Seconds between keepalives")) == L"45", "Changing pages must retain edited values");
            visit("Session");
            RECT full{}; GetWindowRect(s->window, &full);
            SetWindowPos(s->window, nullptr, 0, 0, full.right - full.left, full.bottom - full.top - ui::px(220), SWP_NOMOVE | SWP_NOZORDER);
            check(maxScroll(s) > 0, "Long settings pages must scroll");
            scrollTo(s, maxScroll(s)); check(s->scroll > 0, "Settings scroll position did not change");
            scrollTo(s, 0);
            SetWindowPos(s->window, nullptr, 0, 0, full.right - full.left, full.bottom - full.top, SWP_NOMOVE | SWP_NOZORDER);
            check(GetNextDlgTabItem(s->window, s->search, FALSE) != nullptr, "Dialog keyboard navigation is missing");
            visit("Connection/SSH/Host keys");
            bool opened = false;
            for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT))
                if (clean(ui::value(control)).find(L"Configure host CAs") != std::wstring::npos) {
                    SendMessageW(control, BM_CLICK, 0, 0); opened = true; break;
                }
            check(opened, "Host authorities must be reachable from connection settings");
            visit("Session");
            report(true); s->submit(s->context, 1);
        } else {
            visit("Window"); auto field = input(L"Lines of scrollback");
            check(ui::value(field) == (s->testRun == 3 ? L"16000" : L"12000"), "Apply or Cancel did not preserve the correct settings");
            if (s->testRun < 3) SetWindowTextW(field, s->testRun == 1 ? L"14000" : L"16000");
            captureTestWindow(s->window, (L"ui-settings-live-" + std::to_wstring(s->testRun) + L".bmp").c_str());
            report(true); s->submit(s->context, s->testRun == 2);
        }
    } catch (const std::exception &error) {
        captureTestWindow(s->window, L"ui-settings-failed.bmp");
        KillTimer(s->window, 92); report(false, error.what()); s->submit(s->context, 0);
    }
}
