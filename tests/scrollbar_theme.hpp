#pragma once
// Inspect rendered non-client pixels rather than only checking a theme name.
template<typename Check> unsigned testDarkScrollbars(HWND window, Check require) {
    RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    RECT bounds{}; GetWindowRect(window, &bounds);
    auto dark = [&](RECT area) {
        OffsetRect(&area, -bounds.left, -bounds.top);
        auto dc = GetWindowDC(window);
        unsigned count = 0; unsigned long long sum = 0;
        for (int y = area.top + 2; y < area.bottom - 2; y += 2)
            for (int x = area.left + 2; x < area.right - 2; x += 2) {
                auto color = GetPixel(dc, x, y);
                if (color != CLR_INVALID) { ++count; sum += GetRValue(color) + GetGValue(color) + GetBValue(color); }
            }
        ReleaseDC(window, dc);
        require(count > 0 && sum < (unsigned long long)count * 3 * 160,
                "Native scrollbar or shared corner must render dark pixels");
    };
    SCROLLBARINFO vertical{sizeof(vertical)}, horizontal{sizeof(horizontal)};
    auto visible = [&](LONG id, SCROLLBARINFO &info) {
        return GetScrollBarInfo(window, id, &info) &&
            !(info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) &&
            info.rcScrollBar.right > info.rcScrollBar.left && info.rcScrollBar.bottom > info.rcScrollBar.top;
    };
    unsigned bars = 0;
    if (visible(OBJID_VSCROLL, vertical)) { dark(vertical.rcScrollBar); bars |= 1; }
    if (visible(OBJID_HSCROLL, horizontal)) { dark(horizontal.rcScrollBar); bars |= 2; }
    if (bars == 3) dark({vertical.rcScrollBar.left, horizontal.rcScrollBar.top,
                         vertical.rcScrollBar.right, horizontal.rcScrollBar.bottom});
    return bars;
}
