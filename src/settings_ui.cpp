#include "core.hpp"
#include "ui.hpp"
#include "resources.hpp"
#include "settings_ui.h"
#include <windowsx.h>
#include <cwctype>
#include <memory>
#include <vector>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#endif

namespace {
constexpr int Search = 62001, Navigation = 62002, side = 252;
constexpr wchar_t property[] = L"wShell.Settings";
struct Page { std::string path; std::wstring title, group, description, keywords; };
struct Settings {
    HWND window{}, viewport{}, page{}, search{}, navigation{}, accept{}, cancel{};
    void *context{}; WsSettingsSelect select{}; WsSettingsAccept submit{};
    bool live{}, authorities{}, ready{}, layingOut{}, dragging{};
    HWND listDrag{};
    int selected = -1, contentHeight{}, scroll{}, dragY{}, dragScroll{}, listDragY{}, listDragTop{};
    UINT dpi = 96;
    ULONG_PTR imageToken{};
    std::unique_ptr<Gdiplus::Bitmap> wordmark;
    std::vector<Page> pages;
    std::vector<int> filtered;
#ifdef WOOK_UI_TEST
    int testStep = 0, testChecks = 0, testRun = 0;
#endif
    ~Settings() { wordmark.reset(); if (imageToken) Gdiplus::GdiplusShutdown(imageToken); }
};
Settings *state(HWND hwnd) {
    while (hwnd) {
        if (auto s = (Settings *)GetPropW(hwnd, property)) return s;
        hwnd = GetParent(hwnd);
    }
    return nullptr;
}
int pixels(Settings *s, int n) { return MulDiv(n, s->dpi, 96); }
RECT client(HWND hwnd) { RECT r{}; GetClientRect(hwnd, &r); return r; }
std::wstring clean(std::wstring value) {
    std::wstring result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'&') { if (i + 1 < value.size() && value[i + 1] == L'&') ++i; else continue; }
        result += value[i];
    }
    return result;
}
std::wstring lower(std::wstring text) { for (auto &c : text) c = (wchar_t)towlower(c); return text; }
HBRUSH brush(COLORREF color) {
    static HBRUSH panel = CreateSolidBrush(ui::panel), raised = CreateSolidBrush(ui::raised);
    return color == ui::panel ? panel : raised;
}
void choose(Settings *s, int index) {
    if (index < 0 || index >= (int)s->pages.size()) return;
    s->selected = index; s->scroll = 0;
    auto row = std::find(s->filtered.begin(), s->filtered.end(), index);
    if (row != s->filtered.end()) SendMessageW(s->navigation, LB_SETCURSEL, row - s->filtered.begin(), 0);
    SetWindowPos(s->page, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    s->select(s->context, s->pages[index].path.c_str());
    InvalidateRect(s->window, nullptr, TRUE);
}
void filter(Settings *s) {
    auto query = lower(wook::trim(ui::value(s->search)));
    s->filtered.clear(); SendMessageW(s->navigation, WM_SETREDRAW, FALSE, 0);
    SendMessageW(s->navigation, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)s->pages.size(); ++i) {
        auto &page = s->pages[i];
        if (!query.empty() && page.keywords.find(query) == std::wstring::npos) continue;
        s->filtered.push_back(i);
        int row = (int)SendMessageW(s->navigation, LB_ADDSTRING, 0, (LPARAM)page.title.c_str());
        if (i == s->selected) SendMessageW(s->navigation, LB_SETCURSEL, row, 0);
    }
    SendMessageW(s->navigation, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(s->navigation, nullptr, TRUE); InvalidateRect(s->window, nullptr, TRUE);
}
int maxScroll(Settings *s) { return std::max(0, s->contentHeight - (int)client(s->viewport).bottom); }
void scrollTo(Settings *s, int position) {
    s->scroll = std::clamp(position, 0, maxScroll(s));
    SetWindowPos(s->page, nullptr, 0, -s->scroll, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(s->viewport, nullptr, TRUE);
}
void reveal(Settings *s, HWND control) {
    if (!s || !s->ready || s->layingOut || !IsChild(s->page, control)) return;
    RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, s->viewport, (POINT *)&r, 2);
    int height = client(s->viewport).bottom, margin = pixels(s, 12);
    if (r.top < margin) scrollTo(s, s->scroll + r.top - margin);
    else if (r.bottom > height - margin) scrollTo(s, s->scroll + r.bottom - height + margin);
}
RECT thumb(Settings *s) {
    RECT r = client(s->viewport);
    if (!maxScroll(s)) return {};
    int height = std::max<int>(pixels(s, 36), r.bottom * r.bottom / std::max(1, s->contentHeight));
    int y = (r.bottom - height) * s->scroll / maxScroll(s);
    return {r.right - pixels(s, 8), y, r.right - pixels(s, 2), y + height};
}
void layout(Settings *s, bool rebuild) {
    if (s->layingOut) return;
    s->layingOut = true; ui::dpi = s->dpi;
    RECT r = client(s->window); int width = MulDiv(r.right, 96, s->dpi), height = MulDiv(r.bottom, 96, s->dpi);
    ui::place(s->search, 26, 109, side - 52, 26);
    ui::place(s->navigation, 12, 160, side - 24, std::max(60, height - 210));
    ui::place(s->cancel, width - 300, height - 57, 110, 38);
    ui::place(s->accept, width - 178, height - 57, 150, 38);
    ui::place(s->viewport, side + 22, 134, width - side - 42, height - 214);
    auto viewport = client(s->viewport);
    int pageWidth = viewport.right - pixels(s, 14);
    SetWindowPos(s->page, nullptr, 0, -s->scroll, pageWidth, std::max(s->contentHeight, (int)viewport.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
    s->layingOut = false;
    if (rebuild && s->selected >= 0) {
        int previous = s->scroll;
        s->select(s->context, s->pages[s->selected].path.c_str());
        scrollTo(s, previous);
    }
    InvalidateRect(s->window, nullptr, TRUE);
}
std::wstring listText(HWND hwnd, UINT getLength, UINT getText, int index) {
    auto length = SendMessageW(hwnd, getLength, index, 0);
    if (length < 0 || length > 1024 * 1024) return L"";
    std::wstring text((size_t)length + 1, L'\0'); SendMessageW(hwnd, getText, index, (LPARAM)text.data());
    text.resize((size_t)length); return text;
}
void paintButton(HWND hwnd, HDC dc) {
    RECT r = client(hwnd); auto caption = clean(ui::value(hwnd));
    LONG_PTR type = GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK;
    bool enabled = IsWindowEnabled(hwnd), focused = GetFocus() == hwnd;
    auto bits = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    COLORREF ink = enabled ? ui::text : ui::muted;
    if (type == BS_GROUPBOX) {
        ui::fill(dc, r, ui::bg);
        ui::round(dc, r, ui::panel, ui::raised, 12);
        RECT title = r; InflateRect(&title, -ui::px(16), -ui::px(8)); title.bottom = title.top + ui::px(42);
        ui::label(dc, caption, title, ui::TextSize::body, ui::bright, true, DT_LEFT | DT_WORDBREAK);
        return;
    }
    if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE ||
        type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) {
        ui::fill(dc, r, ui::panel);
        bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) != BST_UNCHECKED;
        int diameter = ui::px(16), y = (r.bottom - diameter) / 2;
        RECT box{ui::px(1), y, ui::px(1) + diameter, y + diameter};
        ui::round(dc, box, checked ? ui::accent : ui::raised, focused ? ui::accent : ui::muted, radio ? 30 : 5);
        if (checked) {
            if (radio) { InflateRect(&box, -ui::px(5), -ui::px(5)); ui::round(dc, box, ui::bg, ui::bg, 20); }
            else ui::label(dc, L"✓", box, ui::TextSize::caption, ui::bg, true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        r.left += ui::px(25); ui::label(dc, caption, r, ui::TextSize::body, ink);
    } else {
        bool primary = GetDlgCtrlID(hwnd) == IDOK;
        ui::fill(dc, r, ui::panel);
        ui::round(dc, r, primary ? ui::accent : bits & BST_PUSHED ? ui::line : ui::raised,
                  focused || primary ? ui::accent : ui::line);
        ui::label(dc, caption, r, ui::TextSize::body, primary ? ui::bg : ink, primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
int listRange(HWND hwnd) {
    int height = std::max(1, (int)SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0));
    return std::max(0, (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0) - (int)client(hwnd).bottom / height);
}
RECT listThumb(HWND hwnd) {
    auto r = client(hwnd); int count = (int)SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
    int row = std::max(1, (int)SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0));
    int height = std::max<int>(ui::px(26), r.bottom * r.bottom / std::max(1, count * row));
    int y = (r.bottom - height) * (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0) / std::max(1, listRange(hwnd));
    return {r.right - ui::px(7), y, r.right - ui::px(3), y + height};
}
LRESULT CALLBACK widgetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_NCDESTROY) { RemoveWindowSubclass(hwnd, widgetProc, id); return DefSubclassProc(hwnd, msg, wp, lp); }
    wchar_t kind[32]{}; GetClassNameW(hwnd, kind, 32);
    if (!wcscmp(kind, L"ListBox")) {
        auto s = state(hwnd);
        if (s && msg == WM_MOUSEWHEEL) {
            int top = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0) - GET_WHEEL_DELTA_WPARAM(wp) * 3 / WHEEL_DELTA;
            SendMessageW(hwnd, LB_SETTOPINDEX, std::clamp(top, 0, listRange(hwnd)), 0);
            InvalidateRect(hwnd, nullptr, TRUE); return 0;
        }
        if (s && msg == WM_LBUTTONDOWN && listRange(hwnd) && GET_X_LPARAM(lp) >= client(hwnd).right - ui::px(12)) {
            auto t = listThumb(hwnd); int y = GET_Y_LPARAM(lp);
            s->listDrag = hwnd; s->listDragY = y; s->listDragTop = (int)SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
            if (y < t.top || y >= t.bottom) {
                s->listDragTop = std::clamp<int>(y * listRange(hwnd) / std::max(1L, client(hwnd).bottom), 0, listRange(hwnd));
                SendMessageW(hwnd, LB_SETTOPINDEX, s->listDragTop, 0);
            }
            SetCapture(hwnd); InvalidateRect(hwnd, nullptr, TRUE); return 0;
        }
        if (s && s->listDrag == hwnd) {
            if (msg == WM_MOUSEMOVE) {
                auto t = listThumb(hwnd);
                int top = s->listDragTop + (GET_Y_LPARAM(lp) - s->listDragY) * listRange(hwnd) /
                    std::max(1L, client(hwnd).bottom - (t.bottom - t.top));
                SendMessageW(hwnd, LB_SETTOPINDEX, std::clamp(top, 0, listRange(hwnd)), 0);
                InvalidateRect(hwnd, nullptr, TRUE); return 0;
            }
            if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) {
                s->listDrag = nullptr; if (GetCapture() == hwnd) ReleaseCapture(); return 0;
            }
        }
        if (msg == WM_PAINT) {
            auto result = DefSubclassProc(hwnd, msg, wp, lp);
            if (listRange(hwnd)) { auto dc = GetDC(hwnd); ui::round(dc, listThumb(hwnd), ui::muted, ui::muted, 4); ReleaseDC(hwnd, dc); }
            return result;
        }
    }
    if (msg == WM_SETFOCUS) reveal(state(hwnd), hwnd);
    if (msg == WM_MOUSEWHEEL && wcscmp(kind, L"ListBox") &&
        (wcscmp(kind, L"ComboBox") || !SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))) {
        if (auto s = state(hwnd)) { scrollTo(s, s->scroll - GET_WHEEL_DELTA_WPARAM(wp) * ui::px(48) / WHEEL_DELTA); return 0; }
    }
    if (msg == WM_ERASEBKGND && (!wcscmp(kind, L"Button") || !wcscmp(kind, L"ComboBox"))) return 1;
    if (msg == WM_PAINT && !wcscmp(kind, L"Button")) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); paintButton(hwnd, dc); EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_PAINT && !wcscmp(kind, L"ComboBox")) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r = client(hwnd);
        ui::fill(dc, r, ui::panel);
        ui::round(dc, r, ui::raised, GetFocus() == hwnd ? ui::accent : ui::line);
        RECT arrow = r; arrow.left = std::max(0L, r.right - ui::px(28));
        ui::label(dc, L"⌄", arrow, ui::TextSize::body, ui::muted, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & 3) == CBS_DROPDOWNLIST) {
            r.left += ui::px(8); r.right = arrow.left;
            auto text = listText(hwnd, CB_GETLBTEXTLEN, CB_GETLBTEXT, (int)SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
            ui::label(dc, text, r, ui::TextSize::body);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::raised); return (LRESULT)brush(ui::raised);
    }
    auto result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == BM_SETCHECK || msg == BM_SETSTATE || msg == WM_ENABLE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS ||
        msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_KEYDOWN || msg == WM_KEYUP ||
        msg == CB_SETCURSEL || msg == CB_SHOWDROPDOWN) InvalidateRect(hwnd, nullptr, TRUE);
    return result;
}
bool drawItem(Settings *s, const DRAWITEMSTRUCT *d) {
    if (d->CtlID == Navigation) {
        ui::fill(d->hDC, d->rcItem, ui::panel);
        if (d->itemID >= s->filtered.size()) return true;
        auto &page = s->pages[s->filtered[d->itemID]];
        bool selected = s->filtered[d->itemID] == s->selected;
        RECT r = d->rcItem; InflateRect(&r, -ui::px(3), -ui::px(3)); r.right -= ui::px(10);
        if (selected) ui::round(d->hDC, r, ui::raised, ui::line);
        if (selected) { RECT bar = r; bar.right = bar.left + ui::px(3); InflateRect(&bar, 0, -ui::px(8)); ui::fill(d->hDC, bar, ui::accent); }
        r.left += ui::px(14); r.top += ui::px(5); r.bottom = r.top + ui::px(23);
        ui::label(d->hDC, page.title, r, ui::TextSize::body, selected ? ui::bright : ui::text, selected);
        r.top += ui::px(23); r.bottom += ui::px(20);
        ui::label(d->hDC, page.group, r, ui::TextSize::caption, selected ? ui::accent : ui::muted);
        return true;
    }
    if (d->CtlType == ODT_LISTBOX || d->CtlType == ODT_COMBOBOX) {
        bool selected = d->itemState & ODS_SELECTED;
        ui::fill(d->hDC, d->rcItem, selected ? ui::line : ui::raised);
        if (d->itemID == (UINT)-1) return true;
        auto text = listText(d->hwndItem, d->CtlType == ODT_LISTBOX ? LB_GETTEXTLEN : CB_GETLBTEXTLEN,
                             d->CtlType == ODT_LISTBOX ? LB_GETTEXT : CB_GETLBTEXT, d->itemID);
        RECT r = d->rcItem; r.left += ui::px(7); r.right -= ui::px(12);
        ui::label(d->hDC, text, r, ui::TextSize::body, selected ? ui::bright : ui::text, false,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_EXPANDTABS | DT_END_ELLIPSIS);
        if (d->itemState & ODS_FOCUS) { RECT focus = d->rcItem; InflateRect(&focus, -2, -2); DrawFocusRect(d->hDC, &focus); }
        return true;
    }
    return false;
}
INT_PTR CALLBACK pageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = state(hwnd); if (!s) return FALSE;
    if (msg == WM_ERASEBKGND) { ui::fill((HDC)wp, client(hwnd), ui::bg); return TRUE; }
    if (msg == WM_COMMAND || msg == WM_DRAWITEM || msg == WM_MEASUREITEM || msg == WM_NOTIFY || msg >= 0xC000 ||
        msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || msg == WM_LBUTTONUP)
        return SendMessageW(s->window, msg, wp, lp);
    if (msg == WM_MOUSEWHEEL) { scrollTo(s, s->scroll - GET_WHEEL_DELTA_WPARAM(wp) * ui::px(48) / WHEEL_DELTA); return TRUE; }
    return FALSE;
}
LRESULT CALLBACK viewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = state(hwnd); if (!s) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); ui::fill(dc, client(hwnd), ui::bg);
        if (maxScroll(s)) ui::round(dc, thumb(s), s->dragging ? ui::accent : ui::line, s->dragging ? ui::accent : ui::line, 6);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_MOUSEWHEEL) { scrollTo(s, s->scroll - GET_WHEEL_DELTA_WPARAM(wp) * ui::px(48) / WHEEL_DELTA); return 0; }
    if (msg == WM_LBUTTONDOWN && maxScroll(s)) {
        auto t = thumb(s); int y = GET_Y_LPARAM(lp);
        if (y >= t.top && y < t.bottom) { s->dragging = true; s->dragY = y; s->dragScroll = s->scroll; SetCapture(hwnd); }
        else scrollTo(s, s->scroll + (y < t.top ? -1 : 1) * client(hwnd).bottom);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && s->dragging) {
        auto r = client(hwnd), t = thumb(s);
        scrollTo(s, s->dragScroll + (GET_Y_LPARAM(lp) - s->dragY) * maxScroll(s) / std::max(1L, r.bottom - (t.bottom - t.top))); return 0;
    }
    if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED) { s->dragging = false; if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void paint(Settings *s, HDC dc) {
    RECT r = client(s->window); int width = MulDiv(r.right, 96, s->dpi), height = MulDiv(r.bottom, 96, s->dpi);
    ui::fill(dc, r, ui::bg); ui::fill(dc, ui::rect(0, 0, side, height), ui::panel);
    ui::fill(dc, ui::rect(side, 0, 1, height), ui::line);
    if (s->wordmark) {
        Gdiplus::Graphics g(dc); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(s->wordmark.get(), ui::px(10), ui::px(6), ui::px(224), ui::px(74));
    }
    ui::label(dc, s->authorities ? L"HOST AUTHORITIES" : s->live ? L"SESSION SETTINGS" : L"CONNECTION SETTINGS", ui::rect(24, 71, 218, 21), ui::TextSize::caption, ui::muted, true);
    ui::round(dc, ui::rect(18, 101, side - 36, 42), ui::raised);
    ui::label(dc, std::to_wstring(s->filtered.size()) + L" sections", ui::rect(26, height - 38, 195, 23), ui::TextSize::caption, ui::muted);
    if (s->filtered.empty()) ui::label(dc, L"No matching settings.", ui::rect(24, 180, side - 40, 28), ui::TextSize::caption, ui::muted);
    if (s->selected >= 0) {
        auto &page = s->pages[s->selected];
        ui::label(dc, page.group, ui::rect(side + 36, 24, width - side - 70, 22), ui::TextSize::caption, ui::accent, true);
        ui::label(dc, page.title, ui::rect(side + 36, 51, width - side - 70, 38), ui::TextSize::title, ui::bright, true);
        ui::label(dc, page.description, ui::rect(side + 36, 94, width - side - 70, 32), ui::TextSize::body, ui::muted);
    }
    ui::fill(dc, ui::rect(side, height - 76, width - side, 76), ui::panel);
    ui::fill(dc, ui::rect(side, height - 76, width - side, 1), ui::line);
    ui::label(dc, s->authorities ? L"Save each authority to keep changes." : s->live ? L"Applies to this connection." : L"Configure, then connect.",
              ui::rect(side + 32, height - 58, width - side - 360, 38), ui::TextSize::caption, ui::muted);
}
#ifdef WOOK_UI_TEST
#include "../tests/settings_smoke.inc"
#endif
}

extern "C" HWND wsSettingsBegin(HWND window, WsSettingsKind kind, void *context, WsSettingsSelect select, WsSettingsAccept accept) {
    auto s = std::make_unique<Settings>(); s->window = window; s->live = kind == WS_SETTINGS_SESSION;
    s->authorities = kind == WS_SETTINGS_AUTHORITIES; s->context = context;
    s->select = select; s->submit = accept; s->dpi = GetDpiForWindow(window); ui::dpi = s->dpi;
#ifdef WOOK_UI_TEST
    static int liveRuns = 0;
    if (s->live) s->testRun = ++liveRuns;
#endif
    SetPropW(window, property, s.get()); ui::dark(window);
    SetWindowTextW(window, s->authorities ? L"wShell · Host authorities" : s->live ? L"wShell · Session settings" : L"wShell · Connection settings");
    SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101)));
    auto style = GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN | WS_THICKFRAME | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(window, GWL_STYLE, style);
    Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&s->imageToken, &input, nullptr);
    try { s->wordmark = wook::loadWordmark(); } catch (...) { /* Header text remains available if resources cannot be read. */ }
    WNDCLASSW wc{}; wc.lpfnWndProc = viewportProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShellSettingsViewport"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    s->search = ui::edit(window, L"Search settings…", Search);
    s->navigation = ui::control(window, L"LISTBOX", L"", Navigation, LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
    SetWindowSubclass(s->navigation, widgetProc, 1, 0);
    SendMessageW(s->navigation, LB_SETITEMHEIGHT, 0, pixels(s.get(), 59));
    s->viewport = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                  0, 0, 100, 100, window, nullptr, wc.hInstance, nullptr);
    SetPropW(s->viewport, property, s.get());
    s->page = CreateDialogParamW(wc.hInstance, MAKEINTRESOURCEW(106), s->viewport, pageProc, 0);
    if (!s->page) { RemovePropW(window, property); RemovePropW(s->viewport, property); return nullptr; }
    SetPropW(s->page, property, s.get()); SetPropW(s->page, L"wShell.SettingsPage", (HANDLE)1);
    s->cancel = ui::control(window, L"BUTTON", L"Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP);
    s->accept = ui::control(window, L"BUTTON", s->authorities ? L"Done" : s->live ? L"Apply changes" : L"Connect", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP);
    if (s->authorities) ShowWindow(s->cancel, SW_HIDE);
    wsSettingsStyleControl(s->cancel); wsSettingsStyleControl(s->accept); SendMessageW(window, DM_SETDEFID, IDOK, 0);
    RECT monitor{}; MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info); monitor = info.rcWork;
    RECT size{0, 0, pixels(s.get(), 1110), pixels(s.get(), 800)};
    AdjustWindowRectExForDpi(&size, (DWORD)style, FALSE, (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE), s->dpi);
    int w = std::min(size.right - size.left, monitor.right - monitor.left - pixels(s.get(), 24));
    int h = std::min(size.bottom - size.top, monitor.bottom - monitor.top - pixels(s.get(), 24));
    SetWindowPos(window, nullptr, monitor.left + (monitor.right - monitor.left - w) / 2,
                 monitor.top + (monitor.bottom - monitor.top - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    layout(s.get(), false); ShowWindow(s->page, SW_SHOW);
    return s.release()->page;
}
extern "C" void wsSettingsAddPage(HWND window, const char *path, const char *description, const char *keywords) {
    auto s = state(window); if (!s) return;
    Page page; page.path = path; page.description = wook::wide(description);
    auto slash = page.path.rfind('/'); page.title = wook::wide(slash == std::string::npos ? page.path : page.path.substr(slash + 1));
    page.group = slash == std::string::npos ? L"GENERAL" : wook::wide(page.path.substr(0, slash));
    for (auto &c : page.group) if (c == L'/') c = L'·'; else c = (wchar_t)towupper(c);
    const struct { const char *path; const wchar_t *title, *description; } names[] = {
        {"Session", L"Connection", L"Choose your destination and manage saved hosts."},
        {"Session/Logging", L"Session logs", L"Choose what to record and where to save it."},
        {"Terminal", L"Terminal", L"Set the behavior of your terminal and remote applications."},
        {"Terminal/Keyboard", L"Keyboard", L"Make shortcuts and special keys work the way you expect."},
        {"Terminal/Bell", L"Notifications", L"Choose how your terminal gets your attention."},
        {"Terminal/Features", L"Terminal features", L"Control features that remote applications may request."},
        {"Window", L"Window & scrollback", L"Adjust terminal dimensions and retained output."},
        {"Window/Appearance", L"Font & appearance", L"Tune your terminal font, cursor and spacing."},
        {"Window/Behaviour", L"Window behavior", L"Choose how the session window responds."},
        {"Window/Translation", L"Text encoding", L"Keep international text and line drawing readable."},
        {"Window/Selection", L"Selection & clipboard", L"Control text selection, copying and pasting."},
        {"Window/Selection/Copy", L"Copy formatting", L"Choose which characters and formatting are copied."},
        {"Window/Colours", L"Terminal colors", L"Customize the terminal palette and color behavior."},
        {"Connection", L"Network", L"Keep sessions responsive and configure network behavior."},
        {"Connection/Data", L"Login & environment", L"Set your username, terminal type and environment variables."},
        {"Connection/Proxy", L"Proxy", L"Reach your server through a proxy or jump host."},
        {"Connection/SSH", L"SSH session", L"Configure your encrypted connection and remote command."},
        {"Connection/SSH/Auth", L"Authentication", L"Choose how you authenticate to the server."},
        {"Connection/SSH/Auth/Credentials", L"SSH keys", L"Choose a private key and optional SSH certificate."},
        {"Connection/SSH/Auth/GSSAPI", L"GSSAPI", L"Configure integrated authentication for your environment."},
        {"Connection/SSH/Host keys", L"Host verification", L"Manage how wShell verifies the server's identity."},
        {"Connection/SSH/Tunnels", L"Port forwarding", L"Route local or remote ports through your SSH connection."},
        {"Connection/SSH/Cipher", L"Encryption", L"Set your preferred encryption algorithms."},
        {"Connection/SSH/Kex", L"Key exchange", L"Choose key exchange algorithms and rekeying limits."},
        {"Connection/SSH/TTY", L"Remote terminal", L"Set terminal modes sent to the remote server."},
        {"Connection/SSH/X11", L"X11 forwarding", L"Forward remote graphical applications to your display."},
        {"Connection/SSH/Bugs", L"Server compatibility", L"Adjust workarounds for server implementation differences."},
        {"Connection/SSH/More bugs", L"More compatibility", L"Configure additional server compatibility workarounds."},
        {"Connection/Serial", L"Serial port", L"Choose a device, speed and flow control."},
        {"Connection/Telnet", L"Telnet", L"Configure Telnet negotiation and terminal behavior."},
        {"Connection/Rlogin", L"Rlogin", L"Set login details for an Rlogin connection."},
        {"Connection/SUPDUP", L"SUPDUP", L"Configure terminal capabilities for a SUPDUP connection."}
    };
    for (auto &name : names) if (page.path == name.path) { page.title = name.title; page.description = name.description; break; }
    if (s->authorities) { page.title = L"Host authorities"; page.group = L"SSH · TRUST"; page.description = L"Manage authorities trusted to certify your SSH servers."; }
    page.keywords = lower(wook::wide(path) + L" " + page.title + L" " + page.description + L" " + wook::wide(keywords));
    s->pages.push_back(std::move(page));
}
extern "C" void wsSettingsReady(HWND window) {
    auto s = state(window); if (!s) return;
    filter(s); s->ready = true;
    if (!s->pages.empty()) { choose(s, 0); SendMessageW(s->navigation, LB_SETCURSEL, 0, 0); }
    SetFocus(s->search);
#ifdef WOOK_UI_TEST
    if (GetEnvironmentVariableW(L"WOOK_TEST_PORT", nullptr, 0)) SetTimer(window, 92, 180, nullptr);
#endif
}
extern "C" void wsSettingsPageReady(HWND window, const char *) {
    auto s = state(window); if (!s) return;
    int bottom = 0;
    std::vector<HWND> controls;
    for (HWND control = GetWindow(s->page, GW_CHILD); control; control = GetWindow(control, GW_HWNDNEXT)) {
        RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, s->page, (POINT *)&r, 2);
        bottom = std::max(bottom, (int)r.bottom); controls.push_back(control);
    }
    for (auto control : controls) wsSettingsStyleControl(control);
    s->contentHeight = bottom + pixels(s, 18); layout(s, false); scrollTo(s, s->scroll);
}
extern "C" void wsSettingsStyleControl(HWND control) {
    auto s = state(control); if (!s) return;
    ui::dpi = s->dpi;
    wchar_t kind[32]{}; GetClassNameW(control, kind, 32);
    SetWindowTheme(control, L"", L"");
    SendMessageW(control, WM_SETFONT, (WPARAM)ui::font(), TRUE);
    auto style = GetWindowLongPtrW(control, GWL_STYLE);
    if (!wcscmp(kind, L"Button") && (style & BS_TYPEMASK) == BS_GROUPBOX) {
        SetWindowLongPtrW(control, GWL_STYLE, style | WS_CLIPSIBLINGS);
        SetWindowPos(control, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (!wcscmp(kind, L"Edit") || !wcscmp(kind, L"ListBox") || !wcscmp(kind, L"ComboBox")) {
        SetWindowLongPtrW(control, GWL_EXSTYLE, GetWindowLongPtrW(control, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
        SetWindowLongPtrW(control, GWL_STYLE, style & ~WS_BORDER);
        SetWindowPos(control, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        if (!wcscmp(kind, L"Edit")) SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(ui::px(6), ui::px(6)));
        if (!wcscmp(kind, L"ListBox")) {
            ShowScrollBar(control, SB_VERT, FALSE); SendMessageW(control, LB_SETITEMHEIGHT, 0, ui::px(22));
        }
        if (!wcscmp(kind, L"ComboBox")) { SendMessageW(control, CB_SETITEMHEIGHT, 0, ui::px(25)); SendMessageW(control, CB_SETITEMHEIGHT, (WPARAM)-1, ui::px(29)); }
    }
    SetWindowSubclass(control, widgetProc, 1, 0);
}
extern "C" BOOL wsSettingsMessage(HWND window, UINT msg, WPARAM wp, LPARAM lp, INT_PTR *result) {
    auto s = state(window); if (!s) return FALSE;
    ui::dpi = s->dpi; *result = 0;
#ifdef WOOK_UI_TEST
    if (msg == WM_TIMER && wp == 92) { runSettingsSmoke(s); return TRUE; }
#endif
    if (msg == WM_ERASEBKGND) { *result = TRUE; return TRUE; }
    if (msg == WM_GETMINMAXINFO) {
        auto info = (MINMAXINFO *)lp; info->ptMinTrackSize = {ui::px(940), ui::px(540)}; return TRUE;
    }
    if (msg == WM_PAINT) { PAINTSTRUCT ps{}; auto dc = BeginPaint(window, &ps); paint(s, dc); EndPaint(window, &ps); return TRUE; }
    if (msg == WM_SIZE) { if (s->ready) layout(s, true); return TRUE; }
    if (msg == WM_DPICHANGED) {
        s->dpi = HIWORD(wp); ui::dpi = s->dpi;
        auto r = (RECT *)lp;
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        for (auto control : {s->search, s->navigation, s->accept, s->cancel, s->page}) SendMessageW(control, WM_SETFONT, (WPARAM)ui::font(), TRUE);
        SendMessageW(s->navigation, LB_SETITEMHEIGHT, 0, ui::px(59)); layout(s, true); return TRUE;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        wchar_t kind[32]{}; GetClassNameW((HWND)lp, kind, 32);
        COLORREF color = msg == WM_CTLCOLORSTATIC && wcscmp(kind, L"Edit") ? ui::panel : ui::raised;
        if ((HWND)lp == s->navigation) color = ui::panel;
        SetTextColor((HDC)wp, IsWindowEnabled((HWND)lp) ? ui::text : ui::muted); SetBkColor((HDC)wp, color);
        *result = (INT_PTR)brush(color); return TRUE;
    }
    if (msg == WM_DRAWITEM && drawItem(s, (DRAWITEMSTRUCT *)lp)) { *result = TRUE; return TRUE; }
    if (msg == WM_MEASUREITEM) { auto item = (MEASUREITEMSTRUCT *)lp; item->itemHeight = ui::px(item->CtlID == Navigation ? 59 : 25); *result = TRUE; return TRUE; }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDOK || id == IDCANCEL) { s->submit(s->context, id == IDOK); return TRUE; }
        if (id == Search && code == EN_CHANGE) { filter(s); return TRUE; }
        if (id == Navigation && code == LBN_SELCHANGE) {
            int index = (int)SendMessageW(s->navigation, LB_GETCURSEL, 0, 0);
            if (index >= 0 && index < (int)s->filtered.size()) choose(s, s->filtered[index]);
            SetFocus(s->navigation); return TRUE;
        }
    }
    return FALSE;
}
extern "C" void wsSettingsEnd(HWND window) {
    auto s = (Settings *)RemovePropW(window, property); if (!s) return;
    RemovePropW(s->page, property); RemovePropW(s->viewport, property); delete s;
}
