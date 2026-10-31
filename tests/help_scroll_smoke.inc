// Read the displayed pixels without invalidating the control first. The usual
// screenshot helper forces a repaint and would hide a damaged scroll frame.
static std::vector<DWORD> helpTextPixels(HWND body) {
    RECT r{}; GetClientRect(body, &r);
    auto source = GetDC(body), target = CreateCompatibleDC(source);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.right; info.bmiHeader.biHeight = -r.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void *bits{}; auto bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(target); ReleaseDC(body, source);
        throw std::runtime_error("Cannot read help text pixels");
    }
    auto old = SelectObject(target, bitmap);
    BitBlt(target, 0, 0, r.right, r.bottom, source, 0, 0, SRCCOPY); GdiFlush();
    std::vector<DWORD> result((DWORD *)bits, (DWORD *)bits + r.right * r.bottom);
    for (auto &pixel : result) pixel &= 0x00ffffff;
    SelectObject(target, old); DeleteObject(bitmap); DeleteDC(target); ReleaseDC(body, source);
    return result;
}
template<typename Check> void testHelpScrolling(App &app, Check require) {
    auto refresh = [](HWND window) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    };
    auto intact = [&](HWND body, const char *message) {
        UpdateWindow(body); auto displayed = helpTextPixels(body);
        refresh(body); auto fresh = helpTextPixels(body);
        require(displayed == fresh, message);
        require(std::adjacent_find(fresh.begin(), fresh.end(), std::not_equal_to<DWORD>()) != fresh.end(),
                "Scrolled help text must not be a blank background");
    };
    auto staleBand = [](HWND body) {
        // Fault injection: model a strip retained from the previous viewport.
        // A small scroll must reconstruct it, not carry stale pixels forward.
        RECT r{}; GetClientRect(body, &r); auto dc = GetDC(body);
        BitBlt(dc, 0, r.bottom / 2, r.right, 24, dc, 0, 0, SRCCOPY);
        ReleaseDC(body, dc);
    };
    auto common = [&](HWND body) {
        auto original = ui::value(body);
        require((GetWindowLongPtrW(body, GWL_STYLE) & (ES_READONLY | ES_MULTILINE)) == (ES_READONLY | ES_MULTILINE),
                "Help text must retain native read-only multiline semantics");
        SendMessageW(body, EM_LINESCROLL, 0, -100000); refresh(body);
        for (int lines : {1, 3, -2, 7, -5, 100000, -100000}) {
            SendMessageW(body, EM_LINESCROLL, 0, lines);
            intact(body, "Line scrolling must match a complete text repaint");
        }
        SendMessageW(body, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        intact(body, "Wheel scrolling must match a complete text repaint");
        SendMessageW(body, WM_KEYDOWN, VK_NEXT, 1);
        intact(body, "Page Down must not leave broken help text");
        SendMessageW(body, EM_SETSEL, 0, 20);
        DWORD first{}, last{}; SendMessageW(body, EM_GETSEL, (WPARAM)&first, (LPARAM)&last);
        require(first == 0 && last == 20, "Help text selection must remain available for copying");
        SendMessageW(body, WM_CHAR, L'X', 1);
        require(ui::value(body) == original, "Typing must not modify help or license notices");
        SendMessageW(body, EM_SETSEL, 0, 0);
        SendMessageW(body, EM_LINESCROLL, 0, -100000); refresh(body);
        staleBand(body); SendMessageW(body, EM_LINESCROLL, 0, 1);
        intact(body, "Scrolling must discard a stale text strip from the previous viewport");
    };
    messageTestTicks = 0; messageTestError.clear();
    messageTestAction = [&](HWND dialog) {
        HWND body = GetDlgItem(dialog, 1000); refresh(dialog);
        require(ui::value(dialog) == L"Keyboard shortcuts", "Help command must open the keyboard reference");
        common(body);
        SendMessageW(body, EM_LINESCROLL, 0, -100000);
        RECT r{}; GetWindowRect(body, &r); MapWindowPoints(nullptr, dialog, (POINT *)&r, 2);
        int x = r.right + ui::px(14), start = r.top + ui::px(4);
        SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, start));
        require(GetCapture() == dialog, "Shortcut scrollbar thumb must capture its drag");
        for (int offset : {12, 45, 90, 30, 110, 0}) {
            SendMessageW(dialog, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, start + ui::px(offset)));
            intact(body, "Shortcut text must be intact during each thumb drag frame");
        }
        SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(x, start));
        require(GetCapture() != dialog, "Shortcut scrollbar must release capture");
        captureTestWindow(dialog, L"ui-help-shortcuts.bmp");
        PostMessageW(dialog, WM_CLOSE, 0, 0);
    };
    messageTestTimer = SetTimer(nullptr, 0, 120, driveMessageTest);
    require(messageTestTimer != 0, "Cannot start help scroll driver");
    SendMessageW(app.hwnd, WM_COMMAND, Shortcuts, 0);
    KillTimer(nullptr, messageTestTimer); messageTestAction = {};
    if (!messageTestError.empty()) throw std::runtime_error(messageTestError);

    SendMessageW(app.hwnd, WM_COMMAND, MenuLicenses, 0);
    HWND licenses = FindWindowW(L"wShellLicenses", nullptr);
    require(licenses && GetWindow(licenses, GW_OWNER) == app.hwnd, "Help command must open the embedded license notices");
    HWND body = GetDlgItem(licenses, 1); refresh(licenses);
    require(ui::value(body).find(L"SIL OPEN FONT LICENSE") != std::wstring::npos,
            "License viewer must contain the complete bundled notices");
    common(body);
    for (int line : {20, 75, 40, 200, 1}) {
        // SB_THUMBPOSITION supplies a position in the message. THUMBTRACK
        // can instead consult the native scrollbar's in-progress track state.
        SendMessageW(body, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, line), 0);
        require(SendMessageW(body, EM_GETFIRSTVISIBLELINE, 0, 0) == line,
                "License scrollbar must reach each requested position");
        intact(body, "Moving the license scrollbar must not leave stale text");
    }
    SendMessageW(body, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 20), 0);
    SendMessageW(body, WM_VSCROLL, SB_ENDSCROLL, 0);
    require(SendMessageW(body, EM_GETFIRSTVISIBLELINE, 0, 0) > 0, "License thumb must move to later notices");
    SendMessageW(body, WM_HSCROLL, SB_LINERIGHT, 0);
    require(GetScrollPos(body, SB_HORZ) > 0, "License horizontal scrolling must remain available");
    intact(body, "License horizontal scrolling must not leave stale text");
    SendMessageW(body, WM_HSCROLL, SB_LEFT, 0);
    for (int width : {480, 900}) {
        SetWindowPos(licenses, nullptr, 0, 0, ui::px(width), ui::px(480), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        intact(body, "Resizing a scrolled license window must repaint the entire text area");
    }
    captureTestWindow(licenses, L"ui-help-licenses.bmp"); DestroyWindow(licenses);
}
