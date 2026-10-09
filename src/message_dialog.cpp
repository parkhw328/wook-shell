#include "core.hpp"
#include "ui.hpp"
#include "message_dialog.h"
#include "../build/version.h"
#include <shellapi.h>
#include <vector>

// The shared PuTTY helper keeps its native fallback for console-only tools.
extern "C" int (*wshellMessageBoxIndirect)(const MSGBOXPARAMSW *);

namespace {
constexpr int bodyId = 1000, detailsId = 1001, emailId = 1010, githubId = 1011, sourceId = 1012;
struct Choice { int id; const wchar_t *label; };
struct Message {
    HWND body{};
    std::wstring title, text, details;
    std::vector<Choice> choices;
    MSGBOXPARAMSW params{};
    UINT dpi = 96;
    int result = 0, defaultId = IDOK, cancelId = IDCANCEL;
    bool done = false, about = false, hostKey = false;
    int wheel = 0, dragY = 0, dragTop = 0;
    bool dragging = false;
};
struct DpiScope {
    UINT previous = ui::dpi;
    explicit DpiScope(UINT dpi) { ui::dpi = dpi; }
    ~DpiScope() { ui::dpi = previous; }
};
const wchar_t *contactUrl(int id) {
    switch (id) {
    case emailId: return L"mailto:parkhw328@gmail.com";
    case githubId: return L"https://github.com/parkhw328";
    case sourceId: return L"https://github.com/parkhw328/wook-shell";
    default: return nullptr;
    }
}
struct ScrollPosition { RECT track, thumb; int first, maximum, page; };
ScrollPosition scrollPosition(HWND hwnd, Message &p) {
    RECT body{}; GetWindowRect(p.body, &body); MapWindowPoints(nullptr, hwnd, (POINT *)&body, 2);
    auto dc = GetDC(p.body); auto old = SelectObject(dc, (HFONT)SendMessageW(p.body, WM_GETFONT, 0, 0));
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics); SelectObject(dc, old); ReleaseDC(p.body, dc);
    int page = std::max(1L, (body.bottom-body.top) / std::max(1L, metrics.tmHeight));
    int lines = (int)SendMessageW(p.body, EM_GETLINECOUNT, 0, 0);
    int first = (int)SendMessageW(p.body, EM_GETFIRSTVISIBLELINE, 0, 0), maximum = std::max(0, lines-page);
    RECT track{body.right+ui::px(10), body.top, body.right+ui::px(18), body.bottom};
    int height = track.bottom-track.top;
    int thumbHeight = std::min(height, std::max(ui::px(24), MulDiv(height, page, std::max(1, lines))));
    int top = track.top + MulDiv(height-thumbHeight, std::min(first, maximum), std::max(1, maximum));
    return {track, {track.left, top, track.right, top+thumbHeight}, first, maximum, page};
}
void scrollTo(HWND hwnd, Message &p, int line) {
    auto s = scrollPosition(hwnd, p);
    SendMessageW(p.body, EM_LINESCROLL, 0, std::clamp(line, 0, s.maximum)-s.first);
    InvalidateRect(hwnd, &s.track, FALSE);
}
void wheelScroll(HWND hwnd, Message &p, WPARAM wp) {
    p.wheel += (short)HIWORD(wp);
    int steps = p.wheel / WHEEL_DELTA; p.wheel %= WHEEL_DELTA;
    UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    auto s = scrollPosition(hwnd, p);
    scrollTo(hwnd, p, s.first - steps * (lines == WHEEL_PAGESCROLL ? s.page : (int)lines));
}
LRESULT CALLBACK bodyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto &p = *reinterpret_cast<Message *>(data); DpiScope dpi(p.dpi);
    if (msg == WM_NCDESTROY) { RemoveWindowSubclass(hwnd, bodyProc, id); return DefSubclassProc(hwnd, msg, wp, lp); }
    if (msg == WM_MOUSEWHEEL) { wheelScroll(GetParent(hwnd), p, wp); return 0; }
    auto result = DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_KEYDOWN || msg == WM_LBUTTONUP || msg == EM_LINESCROLL) {
        auto s = scrollPosition(GetParent(hwnd), p); InvalidateRect(GetParent(hwnd), &s.track, FALSE);
    }
    return result;
}
void layout(HWND hwnd, Message &p) {
    RECT r{}; GetClientRect(hwnd, &r);
    int width = MulDiv(r.right, 96, p.dpi), height = MulDiv(r.bottom, 96, p.dpi);
    int links = p.about ? 138 : 0, footer = height - 76;
    ui::place(p.body, 28, 98, width - 76, std::max(30, footer - links - 118));
    if (p.about) {
        ui::place(GetDlgItem(hwnd, emailId), 28, footer - 134, width - 56, 38);
        ui::place(GetDlgItem(hwnd, githubId), 28, footer - 90, width - 56, 38);
        ui::place(GetDlgItem(hwnd, sourceId), 28, footer - 46, width - 56, 38);
    }
    int x = width - 28;
    for (auto it = p.choices.rbegin(); it != p.choices.rend(); ++it) {
        int buttonWidth = p.hostKey && it->id == IDNO ? 150 : 108;
        x -= buttonWidth;
        ui::place(GetDlgItem(hwnd, it->id), x, footer + 20, buttonWidth, 36); x -= 12;
    }
    if (!p.details.empty()) ui::place(GetDlgItem(hwnd, detailsId), 28, footer + 20, 110, 36);
    if (p.params.dwStyle & MB_HELP) ui::place(GetDlgItem(hwnd, IDHELP), 28, footer + 20, 90, 36);
}
void help(HWND hwnd, Message &p) {
    HELPINFO info{sizeof(info)}; info.iContextType = HELPINFO_WINDOW; info.hItemHandle = hwnd;
    info.dwContextId = p.params.dwContextHelpId; GetCursorPos(&info.MousePos);
    if (p.params.lpfnMsgBoxCallback) p.params.lpfnMsgBoxCallback(&info);
    else if (p.params.hwndOwner) SendMessageW(p.params.hwndOwner, WM_HELP, 0, (LPARAM)&info);
}
LRESULT CALLBACK messageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto p = reinterpret_cast<Message *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        p = static_cast<Message *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
    DpiScope dpi(p->dpi);
    switch (msg) {
    case WM_CREATE:
        ui::dark(hwnd);
        p->body = ui::control(hwnd, L"EDIT", L"", bodyId,
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP);
        SetWindowSubclass(p->body, bodyProc, 1, (DWORD_PTR)p);
        SendMessageW(p->body, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        SetWindowTextW(p->body, p->text.c_str());
        for (auto choice : p->choices) ui::button(hwnd, choice.label, choice.id);
        if (!p->details.empty()) ui::button(hwnd, p->about ? L"Shortcuts" : L"More info", detailsId);
        if (p->params.dwStyle & MB_HELP) ui::button(hwnd, L"Help", IDHELP);
        if (p->about) {
            ui::button(hwnd, L"Email     parkhw328@gmail.com", emailId);
            ui::button(hwnd, L"GitHub    github.com/parkhw328", githubId);
            ui::button(hwnd, L"Source    github.com/parkhw328/wook-shell", sourceId);
            for (int id : {emailId, githubId, sourceId}) SetPropW(GetDlgItem(hwnd, id), L"wShell.LinkTarget", (HANDLE)contactUrl(id));
        }
        if (!p->cancelId) EnableMenuItem(GetSystemMenu(hwnd, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        layout(hwnd, *p); return 0;
    case WM_SIZE: layout(hwnd, *p); return 0;
    case WM_DPICHANGED: {
        p->dpi = HIWORD(wp); DpiScope updated(p->dpi);
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, (WPARAM)ui::font(), TRUE);
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE); return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == bodyId && HIWORD(wp) == EN_VSCROLL) { auto s = scrollPosition(hwnd, *p); InvalidateRect(hwnd, &s.track, FALSE); return 0; }
        if (HIWORD(wp) != BN_CLICKED) return 0;
        if (p->about && contactUrl(id)) {
            auto result = (INT_PTR)ShellExecuteW(hwnd, L"open", contactUrl(id), nullptr, nullptr, SW_SHOWNORMAL);
            if (result <= 32) wsMessageBoxW(hwnd, L"No application could open this link. Use the address shown in About with your browser or mail app.",
                                          L"Unable to open link", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (id == detailsId) { wsMessageBoxW(hwnd, p->details.c_str(), p->about ? L"Keyboard shortcuts" : L"Server key details", MB_OK | MB_ICONINFORMATION); return 0; }
        if (id == IDHELP) { help(hwnd, *p); return 0; }
        for (auto choice : p->choices) if (choice.id == id) { p->result = id; p->done = true; }
        return 0;
    }
    case WM_CLOSE:
        if (p->cancelId) { p->result = p->cancelId; p->done = true; }
        return 0;
    case WM_DESTROY: p->done = true; return 0;
    case WM_MOUSEWHEEL: wheelScroll(hwnd, *p, wp); return 0;
    case WM_LBUTTONDOWN: {
        auto s = scrollPosition(hwnd, *p); POINT point{(short)LOWORD(lp), (short)HIWORD(lp)};
        RECT hit = s.track; InflateRect(&hit, ui::px(5), 0);
        if (s.maximum && PtInRect(&hit, point)) {
            if (point.y >= s.thumb.top && point.y < s.thumb.bottom) {
                p->dragging = true; p->dragY = point.y; p->dragTop = s.first; SetCapture(hwnd);
            } else scrollTo(hwnd, *p, s.first + (point.y < s.thumb.top ? -s.page : s.page));
            InvalidateRect(hwnd, &hit, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (p->dragging) {
            auto s = scrollPosition(hwnd, *p);
            scrollTo(hwnd, *p, p->dragTop + MulDiv((short)HIWORD(lp)-p->dragY, s.maximum,
                std::max(1L, s.track.bottom-s.track.top-s.thumb.bottom+s.thumb.top)));
        }
        return 0;
    case WM_LBUTTONUP: case WM_CAPTURECHANGED:
        p->dragging = false; if (GetCapture() == hwnd) ReleaseCapture(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_HELP: help(hwnd, *p); return TRUE;
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
        if (p->about && contactUrl((int)wp)) {
            ui::fill(item->hDC, item->rcItem, ui::panel);
            ui::round(item->hDC, item->rcItem, ui::raised, item->itemState & ODS_FOCUS ? ui::accent : ui::line);
            RECT text = item->rcItem; text.left += ui::px(14); text.right -= ui::px(12);
            ui::label(item->hDC, ui::value(item->hwndItem), text, ui::TextSize::caption, ui::text);
        } else ui::drawButton(item, (int)wp == p->defaultId);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::panel); SetDCBrushColor((HDC)wp, ui::panel);
        return (LRESULT)GetStockObject(DC_BRUSH);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        ui::fill(dc, r, ui::panel);
        auto icon = p->params.dwStyle & MB_ICONMASK;
        auto color = icon == MB_ICONERROR ? ui::red : ui::accent;
        ui::fill(dc, {0, 0, r.right, ui::px(3)}, color);
        const wchar_t *kind = p->about ? L"ABOUT / WSHELL" : p->hostKey ? L"SERVER IDENTITY" :
            icon == MB_ICONERROR ? L"ERROR" : icon == MB_ICONWARNING ? L"ATTENTION" :
            p->choices.size() > 1 ? L"CONFIRMATION" : L"INFORMATION";
        ui::label(dc, kind, {ui::px(28), ui::px(19), r.right-ui::px(28), ui::px(38)}, ui::TextSize::caption, color, true);
        ui::label(dc, p->title, {ui::px(28), ui::px(46), r.right-ui::px(28), ui::px(82)}, ui::TextSize::title, ui::bright, true);
        ui::fill(dc, {0, r.bottom-ui::px(76), r.right, r.bottom-ui::px(75)}, ui::line);
        auto s = scrollPosition(hwnd, *p);
        if (s.maximum) ui::round(dc, s.thumb, p->dragging ? ui::accent : ui::muted, p->dragging ? ui::accent : ui::muted, 6);
        EndPaint(hwnd, &ps); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int show(HWND owner, Message &p) {
    if (owner) owner = GetAncestor(owner, GA_ROOT);
    p.dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem(); if (!p.dpi) p.dpi = 96;
    DpiScope dpi(p.dpi);
    WNDCLASSW wc{}; wc.lpfnWndProc = messageProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShell.MessageDialog"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101)); RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    auto work = monitor.rcWork;
    int width = std::min<int>(ui::px(660), work.right-work.left-ui::px(32));
    RECT measured{0, 0, width-ui::px(76), 0};
    auto dc = GetDC(owner); auto font = SelectObject(dc, ui::font());
    DrawTextW(dc, p.text.c_str(), (int)p.text.size(), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, font); ReleaseDC(owner, dc);
    int height = ui::px(p.about ? 336 : 198) + std::clamp((int)measured.bottom + ui::px(8), ui::px(70), ui::px(420));
    height = std::min<int>(height, work.bottom-work.top-ui::px(80));
    RECT size{0,0,width,height};
    AdjustWindowRectExForDpi(&size, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, p.dpi);
    width = size.right-size.left; height = size.bottom-size.top;
    RECT parent = work; if (owner) GetWindowRect(owner, &parent);
    int x = std::clamp((parent.left+parent.right-width)/2, work.left, std::max(work.left, work.right-width));
    int y = std::clamp((parent.top+parent.bottom-height)/2, work.top, std::max(work.top, work.bottom-height));
    HWND previous = GetFocus();
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, p.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x,y,width,height,owner,nullptr,wc.hInstance,&p);
    if (!dialog) return p.cancelId;
    bool enabled = owner && IsWindowEnabled(owner);
    if (enabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOWNORMAL); SetForegroundWindow(dialog); SetFocus(GetDlgItem(dialog, p.defaultId));
    MSG msg{}; bool quit = false;
    while (!p.done && IsWindow(dialog)) {
        int next = GetMessageW(&msg, nullptr, 0, 0);
        if (next <= 0) { quit = next == 0; break; }
        bool ours = msg.hwnd == dialog || IsChild(dialog, msg.hwnd);
        if (ours && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(dialog, WM_CLOSE, 0, 0); continue;
        }
        if (ours && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            auto focus = GetFocus(); int id = GetDlgCtrlID(focus);
            bool button = id == IDHELP || id == detailsId || (p.about && contactUrl(id));
            for (auto choice : p.choices) if (choice.id == id) button = true;
            SendMessageW(dialog, WM_COMMAND, button ? id : p.defaultId, 0); continue;
        }
        if (!ours || !IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (enabled && IsWindow(owner)) EnableWindow(owner, TRUE);
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (enabled && IsWindow(owner)) { SetActiveWindow(owner); if (IsWindow(previous)) SetFocus(previous); }
    if (quit) PostQuitMessage((int)msg.wParam);
    return p.result ? p.result : p.cancelId;
}
std::wstring ansi(const char *text) {
    if (!text) return {};
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring result(count, L'\0');
    if (count) { MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), count); result.pop_back(); }
    return result;
}
}
extern "C" int wsMessageBoxIndirectW(const MSGBOXPARAMSW *params) {
    try {
        Message p; p.params = *params; p.title = params->lpszCaption ? params->lpszCaption : L"wShell";
        p.text = params->lpszText ? params->lpszText : L"";
        switch (params->dwStyle & MB_TYPEMASK) {
        case MB_OKCANCEL: p.choices = {{IDOK,L"OK"},{IDCANCEL,L"Cancel"}}; break;
        case MB_YESNO: p.choices = {{IDYES,L"Yes"},{IDNO,L"No"}}; break;
        case MB_YESNOCANCEL: p.choices = {{IDYES,L"Yes"},{IDNO,L"No"},{IDCANCEL,L"Cancel"}}; break;
        case MB_RETRYCANCEL: p.choices = {{IDRETRY,L"Retry"},{IDCANCEL,L"Cancel"}}; break;
        case MB_ABORTRETRYIGNORE: p.choices = {{IDABORT,L"Abort"},{IDRETRY,L"Retry"},{IDIGNORE,L"Ignore"}}; break;
        case MB_CANCELTRYCONTINUE: p.choices = {{IDCANCEL,L"Cancel"},{IDTRYAGAIN,L"Try again"},{IDCONTINUE,L"Continue"}}; break;
        default: p.choices = {{IDOK,L"OK"}}; break;
        }
        p.cancelId = p.choices.size() == 1 ? IDOK : 0;
        for (auto choice : p.choices) if (choice.id == IDCANCEL) p.cancelId = IDCANCEL;
        size_t selected = (params->dwStyle & MB_DEFMASK) >> 8;
        p.defaultId = p.choices[selected < p.choices.size() ? selected : 0].id;
        return show(params->hwndOwner, p);
    } catch (...) { return MessageBoxIndirectW(params); }
}
extern "C" int wsMessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags) {
    MSGBOXPARAMSW params{sizeof(params)}; params.hwndOwner = owner; params.lpszText = text;
    params.lpszCaption = title; params.dwStyle = flags; return wsMessageBoxIndirectW(&params);
}
extern "C" int wsMessageBoxA(HWND owner, LPCSTR text, LPCSTR title, UINT flags) {
    try { return wsMessageBoxW(owner, ansi(text).c_str(), ansi(title).c_str(), flags); }
    catch (...) { return MessageBoxA(owner, text, title, flags); }
}
extern "C" int wsHostKeyDialog(HWND owner, const char *title, const char *text, const char *details, int changed) {
    try {
        Message p; p.title = wook::wide(title); p.text = wook::wide(text); p.details = wook::wide(details);
        p.hostKey = true; p.params.dwStyle = changed ? MB_ICONERROR : MB_ICONWARNING;
        p.choices = {{IDYES,L"Accept"},{IDNO,L"Connect Once"},{IDCANCEL,L"Cancel"}};
        p.defaultId = p.cancelId = IDCANCEL; return show(owner, p);
    } catch (...) { return IDCANCEL; }
}
extern "C" void wsShowAbout(HWND owner) {
    Message p; p.about = true; p.title = L"wShell " WSHELL_VERSION_WIDE;
    p.text = L"A quiet workspace for your servers.\r\nNative Windows x64 / Portable / MIT License\r\n\r\n"
        L"Created by Hyunwook Park\r\n\r\nPuTTY 0.85 — modified portable build (MIT)\r\n"
        L"Flexoki — Steph Ango (MIT)\r\nJetBrains Mono — SIL OFL 1.1\r\n\r\n"
        L"Full notices: Tools → Open-source licenses.\r\nIndependent project; not affiliated with PuTTY or Termius.";
    p.details = L"Ctrl+Shift+T  New connection\r\nCtrl+Shift+D  Duplicate tab\r\nCtrl+Tab  Next tab\r\nCtrl+Shift+W  Close tab\r\n"
        L"Ctrl+Shift+H  Show / hide hosts\r\nCtrl+Shift+P  Search connections\r\nCtrl+Shift+C / V  Copy / paste\r\n"
        L"Alt+1…9  Switch tabs\r\nF11  Full screen";
    p.choices = {{IDOK,L"Close"}}; p.defaultId = p.cancelId = IDOK; show(owner, p);
}
extern "C" void wsInitializeMessageDialogs(void) { wshellMessageBoxIndirect = wsMessageBoxIndirectW; }
