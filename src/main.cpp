#include "core.hpp"
#include "dialog.hpp"
#include "ui.hpp"
#include "resources.hpp"
#include "key_dialog.hpp"
#include "backup.hpp"
#include "credentials.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#include "keys.h"
#endif

using wook::Profile;
extern "C" int WINAPI wshellTerminalMain(HINSTANCE, HINSTANCE, LPSTR, int);
namespace {
enum { Search = 100, HostList, NewHost, ConnectHost, EditHost, Quick, QuickConnect, Preview,
       Advanced, Duplicate, Reconnect, SessionSettings, Tools, About, SaveCurrent, HomeNew, HomePreview, LocalCmd, LocalPowerShell };
struct Tab {
    Profile profile;
    std::wstring storageName;
    HWND terminal = nullptr;
    HANDLE process = nullptr;
    DWORD pid = 0;
    bool preview = false, transient = false, closing = false, ended = false;
    ~Tab() { if (process) CloseHandle(process); }
};
struct Hit { RECT rect; int index; bool close; };
struct App {
    HWND hwnd = nullptr, controls[24]{};
    HANDLE job = nullptr;
    std::wstring directory;
    std::vector<Profile> profiles;
    std::vector<size_t> filtered;
    std::vector<std::unique_ptr<Tab>> tabs;
    std::vector<Hit> hits;
    int active = -1, tabScroll = 0, dragTab = -1, width = 1200, height = 760;
    POINT dragStart{};
    unsigned sequence = 0;
    bool fullscreen = false, dragging = false;
    WINDOWPLACEMENT placement{sizeof(placement)};
    RECT plus{}, home{};
    HWND control(int id) const { return controls[id - Search]; }
    void layout();
    void paint(HDC dc);
    void refresh();
    void filter();
    void select(int index);
    void connect(const Profile &profile, bool saved, bool preview = false, bool advanced = false);
    void localShell(bool powershell = false);
    void closeTab(int index);
    void command(int code);
    void action(int id);
    void poll();
    void toolsMenu();
    void hostMenu(POINT point);
    void tabMenu(int index, POINT point);
    Profile *selectedHost();
};
#ifdef WOOK_UI_TEST
void runUiSmoke(App &app);
#endif
constexpr int sidebar = 252;
void visible(HWND hwnd, bool show) { ShowWindow(hwnd, show ? SW_SHOWNA : SW_HIDE); }
std::wstring lower(std::wstring text) { std::transform(text.begin(), text.end(), text.begin(), towlower); return text; }
void App::filter() {
    auto query = lower(ui::value(control(Search)));
    auto current = SendMessageW(control(HostList), LB_GETCURSEL, 0, 0);
    std::wstring old;
    if (current >= 0 && (size_t)current < filtered.size()) old = profiles[filtered[current]].name;
    filtered.clear();
    SendMessageW(control(HostList), WM_SETREDRAW, FALSE, 0);
    SendMessageW(control(HostList), LB_RESETCONTENT, 0, 0);
    int selected = 0;
    for (size_t i = 0; i < profiles.size(); ++i) {
        auto &p = profiles[i];
        if (!query.empty() && lower(p.name + L" " + p.host + L" " + p.group + L" " + p.user).find(query) == std::wstring::npos) continue;
        if (p.name == old) selected = (int)filtered.size();
        filtered.push_back(i);
        SendMessageW(control(HostList), LB_ADDSTRING, 0, (LPARAM)p.name.c_str());
    }
    if (!filtered.empty()) SendMessageW(control(HostList), LB_SETCURSEL, selected, 0);
    SendMessageW(control(HostList), WM_SETREDRAW, TRUE, 0);
    InvalidateRect(control(HostList), nullptr, TRUE);
    EnableWindow(control(ConnectHost), !filtered.empty());
    EnableWindow(control(EditHost), !filtered.empty());
    visible(control(HostList), !filtered.empty());
    InvalidateRect(hwnd, nullptr, FALSE);
}
void App::refresh() { auto next = wook::loadProfiles(); filtered.clear(); profiles = std::move(next); filter(); }
Profile *App::selectedHost() {
    auto i = SendMessageW(control(HostList), LB_GETCURSEL, 0, 0);
    return i >= 0 && (size_t)i < filtered.size() ? &profiles[filtered[i]] : nullptr;
}
void App::layout() {
    RECT client; GetClientRect(hwnd, &client);
    width = MulDiv(client.right, 96, ui::dpi); height = MulDiv(client.bottom, 96, ui::dpi);
    ui::place(control(Search), 30, 102, sidebar - 61, 22);
    ui::place(control(NewHost), 18, 151, sidebar - 36, 37);
    ui::place(control(HostList), 10, 231, sidebar - 20, std::max(55, height - 420));
    ui::place(control(ConnectHost), 18, height - 169, 139, 35);
    ui::place(control(EditHost), 166, height - 169, 68, 35);
    ui::place(control(Advanced), 18, height - 122, sidebar - 36, 35);
    ui::place(control(Tools), 18, height - 75, 139, 33);
    ui::place(control(About), 166, height - 75, 68, 33);
    int left = sidebar + 48, span = std::max(360, width - left - 48), card = (span - 17) / 2;
    ui::place(control(Quick), left + 17, 246, std::max(100, span - 166), 25);
    ui::place(control(QuickConnect), left + span - 123, 235, 123, 46);
    ui::place(control(HomeNew), left + 22, 437, card - 44, 40);
    ui::place(control(HomePreview), left + card + 39, 437, card - 44, 40);
    ui::place(control(LocalCmd), left + span - 336, 536, 166, 40);
    ui::place(control(LocalPowerShell), left + span - 160, 536, 138, 40);
    ui::place(control(Duplicate), width - 330, 64, 95, 33);
    ui::place(control(Reconnect), width - 226, 64, 102, 33);
    ui::place(control(SessionSettings), width - 115, 64, 95, 33);
    ui::place(control(SaveCurrent), width - 126, height - 30, 116, 27);
    for (int id : {Quick, QuickConnect, HomeNew, HomePreview, LocalCmd, LocalPowerShell}) visible(control(id), active < 0);
    for (int id : {Duplicate, Reconnect, SessionSettings}) visible(control(id), active >= 0);
    visible(control(SaveCurrent), active >= 0 && !tabs[active]->preview && tabs[active]->transient && tabs[active]->profile.protocol != L"local");
    EnableWindow(control(SessionSettings), active >= 0 && tabs[active]->profile.protocol != L"local");
    for (size_t i = 0; i < tabs.size(); ++i) {
        auto &tab = *tabs[i];
        if (IsWindow(tab.terminal)) {
            if ((int)i == active) {
                ui::place(tab.terminal, sidebar + 1, 113, width - sidebar - 1, std::max(10, height - 148));
                ShowWindow(tab.terminal, SW_SHOWNA);
            } else ShowWindow(tab.terminal, SW_HIDE);
        }
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}
void App::select(int index) {
    active = std::clamp(index, -1, (int)tabs.size() - 1);
    if (active >= 0 && active < tabScroll) tabScroll = active;
    int capacity = std::max(1, (width - sidebar - 162) / 160);
    if (active >= tabScroll + capacity) tabScroll = active - capacity + 1;
    tabScroll = std::clamp(tabScroll, 0, std::max(0, (int)tabs.size() - 1));
    layout();
    if (active >= 0 && IsWindow(tabs[active]->terminal)) SetFocus(tabs[active]->terminal);
    else if (active < 0) SetFocus(control(Quick));
}
void App::paint(HDC dc) {
    ui::fill(dc, ui::rect(0, 0, width, height), ui::bg);
    ui::fill(dc, ui::rect(0, 0, sidebar, height), ui::panel);
    ui::fill(dc, ui::rect(sidebar, 0, 1, height), ui::line);
    ui::label(dc, L"wShell", ui::rect(24, 15, 212, 43), ui::TextSize::title, ui::bright, true);
    ui::label(dc, L"YOUR PERSONAL WORKSPACE", ui::rect(24, 67, 211, 14), ui::TextSize::caption, ui::muted, true);
    ui::round(dc, ui::rect(18, 90, sidebar - 36, 45), ui::raised);
    ui::label(dc, L"SAVED HOSTS", ui::rect(21, 200, 150, 23), ui::TextSize::caption, ui::muted, true);
    ui::label(dc, std::to_wstring(profiles.size()), ui::rect(sidebar - 56, 200, 34, 23), ui::TextSize::caption, ui::muted, false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (filtered.empty()) {
        ui::label(dc, profiles.empty() ? L"No saved hosts yet." : L"No matching hosts.", ui::rect(24, 246, 210, 25), ui::TextSize::body, ui::muted);
        ui::label(dc, L"Add one to get started.", ui::rect(24, 274, 210, 22), ui::TextSize::caption, ui::muted);
    }
    ui::fill(dc, ui::rect(18, height - 188, sidebar - 36, 1), ui::line);
    ui::fill(dc, ui::rect(sidebar + 1, 51, width - sidebar, 1), ui::line);
    hits.clear(); home = ui::rect(sidebar + 9, 9, 96, 35);
    if (active < 0) ui::round(dc, home, ui::raised, ui::line, 8);
    ui::label(dc, L"Workspace", home, ui::TextSize::body, active < 0 ? ui::bright : ui::muted, active < 0, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int x = sidebar + 112, available = width - x - 51;
    int capacity = std::max(1, available / 160);
    int tabWidth = std::min(198, available / std::max(1, std::min(capacity, (int)tabs.size())));
    for (int i = tabScroll; i < (int)tabs.size() && i < tabScroll + capacity; ++i) {
        RECT r = ui::rect(x, 9, tabWidth - 5, 35);
        if (i == active) {
            ui::round(dc, r, ui::raised, ui::line, 8);
            ui::fill(dc, ui::rect(x + 14, 43, tabWidth - 33, 2), ui::accent);
        }
        auto &tab = *tabs[i];
        ui::label(dc, tab.preview ? L"Color preview" : tab.profile.name, ui::rect(x + 13, 9, tabWidth - 49, 35), ui::TextSize::caption, i == active ? ui::bright : ui::muted);
        RECT cross = ui::rect(x + tabWidth - 33, 14, 24, 25);
        ui::label(dc, L"×", cross, ui::TextSize::section, ui::muted, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        hits.push_back({r, i, false}); hits.push_back({cross, i, true}); x += tabWidth;
    }
    plus = ui::rect(width - 40, 11, 30, 31);
    ui::label(dc, L"+", plus, ui::TextSize::title, ui::muted, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (active < 0) {
        int left = sidebar + 48, span = std::max(360, width - left - 48);
        ui::label(dc, L"LESS FRICTION. MORE FLOW.", ui::rect(left, 102, span, 24), ui::TextSize::caption, ui::accent, true);
        ui::label(dc, L"Your servers. One quiet workspace.", ui::rect(left, 138, span, 50), ui::TextSize::title, ui::bright, true);
        ui::label(dc, L"A familiar terminal, with room for every connection.", ui::rect(left, 192, span, 29), ui::TextSize::body, ui::muted);
        ui::round(dc, ui::rect(left, 235, span - 134, 46), ui::raised, ui::line, 12);
        ui::label(dc, L"QUICK CONNECT", ui::rect(left, 299, 140, 23), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"user@hostname:22   or   ssh://user@[::1]:22", ui::rect(left + 139, 299, span - 139, 23), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int gap = 17, card = (span - gap) / 2;
        ui::round(dc, ui::rect(left, 348, card, 145), ui::panel, ui::raised, 16);
        ui::round(dc, ui::rect(left + card + gap, 348, card, 145), ui::panel, ui::raised, 16);
        ui::label(dc, L"A home for every host", ui::rect(left + 22, 365, card - 44, 30), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Save it once. Open it in a new tab.", ui::rect(left + 22, 402, card - 44, 24), ui::TextSize::body, ui::muted);
        ui::label(dc, L"Made for the command line", ui::rect(left + card + gap + 22, 365, card - 44, 30), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Warm colors. Sharp type. Full color.", ui::rect(left + card + gap + 22, 402, card - 44, 24), ui::TextSize::body, ui::muted);
        ui::round(dc, ui::rect(left, 511, span, 84), ui::panel, ui::raised, 16);
        ui::label(dc, L"Local terminal", ui::rect(left + 22, 526, span - 376, 27), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Work on this computer.", ui::rect(left + 22, 558, span - 376, 22), ui::TextSize::body, ui::muted);
        if (height >= 738) {
        int y = std::max(620, height - 142);
        ui::label(dc, L"BUILT TO STAY OUT OF YOUR WAY", ui::rect(left, y, span, 21), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"SSH  /  Local shell  /  Serial     ·     Tabs that travel with you", ui::rect(left, y + 32, span, 26), ui::TextSize::body, ui::text);
        ui::label(dc, L"Ctrl + Shift + T   new connection       Ctrl + Tab   switch tabs", ui::rect(left, y + 66, span, 22), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        auto &tab = *tabs[active];
        ui::label(dc, tab.preview ? L"Terminal preview" : tab.profile.name, ui::rect(sidebar + 22, 61, std::max(100, width - sidebar - 370), 24), ui::TextSize::body, ui::bright, true);
        std::wstring endpoint = tab.preview ? L"Local preview · no connection" : tab.profile.protocol == L"local" ? L"Local terminal · this computer" : tab.profile.protocol + L"  /  " + (tab.profile.user.empty() ? L"" : tab.profile.user + L"@") + tab.profile.host + L":" + std::to_wstring(tab.profile.port);
        ui::label(dc, endpoint, ui::rect(sidebar + 22, 85, std::max(100, width - sidebar - 370), 19), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        ui::fill(dc, ui::rect(sidebar, 112, width - sidebar, 1), ui::raised);
        if (!IsWindow(tab.terminal)) {
            int cx = sidebar + 56;
            ui::label(dc, tab.ended ? L"This session has ended." : tab.closing ? L"Closing session…" : L"Preparing your terminal…", ui::rect(cx, 220, width - cx - 40, 50), ui::TextSize::title, ui::bright, true);
            ui::label(dc, tab.ended ? L"Reconnect to start again, or open another host." : L"Complete any connection or configuration dialog to continue.", ui::rect(cx, 279, width - cx - 40, 30), ui::TextSize::body, ui::muted);
        }
    }
    ui::fill(dc, ui::rect(0, height - 32, width, 32), ui::panel);
    ui::fill(dc, ui::rect(0, height - 33, width, 1), ui::line);
    ui::label(dc, L"●  LOCAL DATA", ui::rect(20, height - 30, 125, 26), ui::TextSize::caption, ui::accent, true);
    ui::label(dc, std::to_wstring(tabs.size()) + L" tabs", ui::rect(158, height - 30, 75, 26), ui::TextSize::caption, ui::muted);
    ui::label(dc, L"Flexoki Dark   /   JetBrains Mono", ui::rect(sidebar + 20, height - 30, 300, 26), ui::TextSize::caption, ui::muted);
    if (active < 0 || !tabs[active]->transient || tabs[active]->preview || tabs[active]->profile.protocol == L"local")
        ui::label(dc, L"NATIVE  ·  WINDOWS x64", ui::rect(width - 200, height - 30, 180, 26), ui::TextSize::caption, ui::muted, false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}
void App::connect(const Profile &profile, bool saved, bool preview, bool advanced) {
    if (tabs.size() >= 32) throw std::runtime_error("Close a tab before opening more than 32 sessions.");
    bool local = profile.protocol == L"local";
    if (!preview && !advanced && !local) wook::validateProfile(profile);
    wchar_t self[32768]; GetModuleFileNameW(nullptr, self, 32768);
    std::wstring engine = self;
    auto tab = std::make_unique<Tab>(); tab->profile = profile; tab->preview = preview; tab->transient = !saved;
    tab->storageName = saved ? profile.name : L"__wook_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
    if (!saved) {
        wchar_t *path = wsPath(L"sessions", wook::utf8(tab->storageName).c_str());
        WsStore *store = wsOpen(path, true); free(path);
        if (!store) throw std::runtime_error("Cannot create temporary session settings.");
        wook::applyTheme(store, profile.fontSize);
        auto set = [&](const char *key, const std::wstring &value) { wsSet(store, key, wook::utf8(value).c_str()); };
        set("HostName", profile.host); set("UserName", profile.user); set("Protocol", profile.protocol);
        set("PortNumber", std::to_wstring(profile.port)); set("PublicKeyFile", profile.keyFile);
        if (profile.protocol == L"serial") { set("SerialLine", profile.host); set("SerialSpeed", std::to_wstring(profile.port)); }
        if (local) {
            wchar_t system[32768]{}; GetSystemDirectoryW(system, 32768);
            auto shell = std::wstring(system) + (profile.host == L"powershell" ? L"\\WindowsPowerShell\\v1.0\\powershell.exe" : L"\\cmd.exe");
            auto options = profile.host == L"powershell" ? L" -NoLogo" : L" /D";
#ifdef WOOK_UI_TEST
            if (profile.host == L"powershell") options = L" -NoLogo -NoProfile";
#endif
            set("RemoteCommand", wook::quoteArg(shell) + options);
            wsSet(store, "RemoteCommandUTF8", "1");
        }
        bool ok = wsSave(store); wsClose(store);
        if (!ok) throw std::runtime_error("Cannot save temporary session settings.");
    }
    std::wstring args = wook::quoteArg(engine) + L" --terminal -load " + wook::quoteArg(tab->storageName);
    if (preview) args += L" -wook-preview";
    if (advanced) args += L" -wook-config";
    if (local) args += L" -wook-local";
    SetEnvironmentVariableW(L"WOOK_PARENT_HWND", std::to_wstring((uintptr_t)hwnd).c_str());
    SetEnvironmentVariableW(L"WOOK_PARENT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
    SetEnvironmentVariableW(L"WOOK_EDIT_SESSION", advanced && saved ? tab->storageName.c_str() : nullptr);
    SetEnvironmentVariableW(L"WOOK_PASSWORD_SESSION", saved ? tab->storageName.c_str() : nullptr);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    wchar_t homeDirectory[32768]{};
    if (local) GetEnvironmentVariableW(L"USERPROFILE", homeDirectory, 32768);
    BOOL created = CreateProcessW(engine.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                  nullptr, local && *homeDirectory ? homeDirectory : directory.c_str(), &startup, &process);
    DWORD error = GetLastError();
    SetEnvironmentVariableW(L"WOOK_PARENT_HWND", nullptr);
    SetEnvironmentVariableW(L"WOOK_PARENT_PID", nullptr);
    SetEnvironmentVariableW(L"WOOK_EDIT_SESSION", nullptr);
    SetEnvironmentVariableW(L"WOOK_PASSWORD_SESSION", nullptr);
    if (!created) throw std::runtime_error("Cannot start the terminal process (Windows error " + std::to_string(error) + ").");
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        throw std::runtime_error("Cannot supervise the terminal process.");
    }
    tab->process = process.hProcess; tab->pid = process.dwProcessId;
    ResumeThread(process.hThread); CloseHandle(process.hThread);
    tabs.push_back(std::move(tab)); select((int)tabs.size() - 1);
}
void App::localShell(bool powershell) {
    Profile profile; profile.name = powershell ? L"PowerShell" : L"Command Prompt";
    profile.protocol = L"local"; profile.host = powershell ? L"powershell" : L"cmd";
    connect(profile, false);
}
void App::closeTab(int index) {
    if (index < 0 || index >= (int)tabs.size()) return;
    auto &tab = *tabs[index];
    if (!tab.ended && !tab.preview && !tab.closing &&
        MessageBoxW(hwnd, L"Close this session? Commands may still be running.", L"Close tab", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (tab.ended || WaitForSingleObject(tab.process, 0) == WAIT_OBJECT_0) {
        if (tab.transient) { wchar_t *path = wsPath(L"sessions", wook::utf8(tab.storageName).c_str()); wsRemove(path); free(path); }
        tabs.erase(tabs.begin() + index); if (active >= index) --active;
        select(std::min(active, (int)tabs.size() - 1)); return;
    }
    tab.closing = true;
    if (IsWindow(tab.terminal)) PostMessageW(tab.terminal, WM_CLOSE, 0, 0);
    else EnumWindows([](HWND child, LPARAM pidValue) -> BOOL {
        DWORD owner; GetWindowThreadProcessId(child, &owner);
        if (owner == (DWORD)pidValue) PostMessageW(child, WM_CLOSE, 0, 0);
        return TRUE;
    }, tab.pid);
    InvalidateRect(hwnd, nullptr, FALSE);
}
void App::poll() {
    for (int i = (int)tabs.size() - 1; i >= 0; --i) {
        auto &tab = *tabs[i];
        if (!tab.ended && WaitForSingleObject(tab.process, 0) == WAIT_OBJECT_0) {
            tab.ended = true; tab.terminal = nullptr;
            if (tab.closing) { closeTab(i); continue; }
            refresh(); layout();
        }
    }
}
void App::command(int code) {
    if (code >= 20 && code < 29) { select(code - 21); return; }
    switch (code) {
    case 1: select(-1); SetFocus(control(Quick)); break;
    case 2:
        if (active >= 0) { auto &t = *tabs[active]; connect(t.profile, !t.transient, t.preview); } break;
    case 3: closeTab(active); break;
    case 4:
        if (active >= 0) {
            auto &t = *tabs[active];
            if (t.ended) {
                Profile p = t.profile; bool saved = !t.transient, preview = t.preview;
                closeTab(active); connect(p, saved, preview);
            } else if (IsWindow(t.terminal) &&
                MessageBoxW(hwnd, L"Restart this connection? The current session will be disconnected.", L"Reconnect", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                PostMessageW(t.terminal, WM_APP + 60, 0, 0);
        } break;
    case 5: select(active >= (int)tabs.size() - 1 ? -1 : active + 1); break;
    case 6: select(active < 0 ? (int)tabs.size() - 1 : active - 1); break;
    case 7: SetFocus(control(Search)); SendMessageW(control(Search), EM_SETSEL, 0, -1); break;
    case 8:
        if (!fullscreen) {
            GetWindowPlacement(hwnd, &placement);
            MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
            SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top, SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(hwnd, &placement);
            SetWindowPos(hwnd, nullptr, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        fullscreen = !fullscreen; break;
    case 9: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case 10: localShell(); break;
    }
}
void App::action(int id) {
    switch (id) {
    case NewHost: case HomeNew: { Profile p; if (editHost(hwnd, p, false)) refresh(); break; }
    case EditHost: {
        auto selected = selectedHost(); if (!selected) break;
        Profile p = *selected; if (editHost(hwnd, p, true)) refresh(); break;
    }
    case ConnectHost: if (auto p = selectedHost()) connect(*p, true); break;
    case QuickConnect: {
        auto e = wook::parseEndpoint(ui::value(control(Quick)));
        Profile p; p.name = e.host; p.host = e.host; p.user = e.user; p.port = e.port; p.protocol = e.protocol;
        if (wook::utf8(p.name).size() > 100) p.name = L"Quick connection";
        connect(p, false); break;
    }
    case Preview: case HomePreview: { Profile p; p.name = L"Color preview"; connect(p, false, true); break; }
    case LocalCmd: localShell(); break;
    case LocalPowerShell: localShell(true); break;
    case Advanced: {
        if (auto p = selectedHost()) connect(*p, true, false, true);
        else { Profile empty; empty.name = L"Connection settings"; connect(empty, false, false, true); }
        break;
    }
    case Duplicate: command(2); break;
    case Reconnect: command(4); break;
    case SessionSettings:
        if (active >= 0 && tabs[active]->profile.protocol != L"local" && IsWindow(tabs[active]->terminal)) PostMessageW(tabs[active]->terminal, WM_SYSCOMMAND, 0x0050, 0);
        break;
    case SaveCurrent:
        if (active >= 0 && !tabs[active]->preview) { Profile p = tabs[active]->profile; if (editHost(hwnd, p, false)) refresh(); }
        break;
    case Tools: toolsMenu(); break;
    case About: showAbout(hwnd); break;
    }
}
void App::toolsMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 10, L"Command Prompt\tCtrl+Shift+L");
    AppendMenuW(menu, MF_STRING, 11, L"PowerShell");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, L"SSH key manager…");
    AppendMenuW(menu, MF_STRING, 6, L"Export settings…");
    AppendMenuW(menu, MF_STRING, 7, L"Import settings…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Open AppData folder");
    AppendMenuW(menu, MF_STRING, 4, L"Open-source licenses…");
    AppendMenuW(menu, MF_STRING, 5, L"Switch tab…");
    RECT r; GetWindowRect(control(Tools), &r);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.top, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (selected == 10 || selected == 11) localShell(selected == 11);
    else if (selected == 1) showKeyManager(hwnd);
    else if (selected == 4) wook::showLicenses(hwnd);
    else if (selected == 6 || selected == 7) {
        wchar_t path[32768] = L"wShell-settings.wshell";
        OPENFILENAMEW ofn{sizeof(ofn)}; ofn.hwndOwner = hwnd; ofn.lpstrFile = path; ofn.nMaxFile = 32768;
        ofn.lpstrTitle = selected == 6 ? L"Export settings — passwords are excluded" : L"Import settings — passwords must be entered again";
        ofn.lpstrFilter = L"wShell settings (*.wshell)\0*.wshell\0\0"; ofn.lpstrDefExt = L"wshell";
        ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (selected == 6 ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (selected == 6 ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) {
            auto result = selected == 6 ? wook::exportSettings(path) : wook::importSettings(path);
            refresh(); MessageBoxW(hwnd, result.c_str(), L"wShell · Settings backup", MB_OK | MB_ICONINFORMATION);
        }
    } else if (selected == 3) {
        wchar_t *root = wsRoot();
        if (!root) throw std::runtime_error("Cannot locate the wShell data folder.");
        std::wstring path = root; free(root);
        ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (selected == 5) {
        menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, 1, L"Workspace");
        for (size_t i = 0; i < tabs.size(); ++i) AppendMenuW(menu, MF_STRING, i + 2, tabs[i]->profile.name.c_str());
        int tab = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.top, 0, hwnd, nullptr);
        DestroyMenu(menu); if (tab) select(tab - 2);
    }
}
void App::hostMenu(POINT point) {
    if (!selectedHost()) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Connect in new tab");
    AppendMenuW(menu, MF_STRING, 2, L"Edit host…");
    AppendMenuW(menu, MF_STRING, 3, L"Duplicate host…");
    AppendMenuW(menu, MF_STRING, 4, L"Connection settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 5, L"Delete host");
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD, point.x, point.y, 0, hwnd, nullptr); DestroyMenu(menu);
    if (choice == 1) action(ConnectHost); else if (choice == 2) action(EditHost); else if (choice == 4) action(Advanced);
    else if (choice == 3) { Profile p = *selectedHost(); p.name += L" copy"; if (editHost(hwnd, p, false)) refresh(); }
    else if (choice == 5 && MessageBoxW(hwnd, L"Delete this saved host? Existing terminal tabs will stay open.", L"Delete host", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        wook::deleteProfile(selectedHost()->name); refresh();
    }
}
void App::tabMenu(int index, POINT point) {
    select(index); HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Duplicate tab\tCtrl+Shift+D");
    AppendMenuW(menu, MF_STRING, 2, L"Reconnect");
    AppendMenuW(menu, MF_STRING, 3, L"Close tab\tCtrl+Shift+W");
    AppendMenuW(menu, MF_STRING, 4, L"Move left");
    AppendMenuW(menu, MF_STRING, 5, L"Move right");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 6, L"Event log");
    AppendMenuW(menu, MF_STRING, 7, L"Copy all terminal output");
    AppendMenuW(menu, MF_STRING, 8, L"Clear scrollback");
    AppendMenuW(menu, MF_STRING, 9, L"Reset terminal");
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD, point.x, point.y, 0, hwnd, nullptr); DestroyMenu(menu);
    if (choice == 1) command(2); else if (choice == 2) command(4); else if (choice == 3) command(3);
    else if (choice == 4 && index > 0) { std::swap(tabs[index], tabs[index - 1]); select(index - 1); }
    else if (choice == 5 && index + 1 < (int)tabs.size()) { std::swap(tabs[index], tabs[index + 1]); select(index + 1); }
    else if (choice >= 6 && choice <= 9 && IsWindow(tabs[index]->terminal)) {
        const int messages[] = {0x0010, 0x0170, 0x0060, 0x0070};
        PostMessageW(tabs[index]->terminal, WM_SYSCOMMAND, messages[choice - 6], 0);
    }
}

LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto *app = (App *)data;
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(app->hwnd, WM_COMMAND, GetDlgCtrlID(hwnd) == Quick ? QuickConnect : ConnectHost, 0); return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE && GetDlgCtrlID(hwnd) != HostList) {
        SetWindowTextW(hwnd, L""); return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, editProc, 1);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *app = (App *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    try {
        if (msg == WM_CREATE) {
            app = (App *)((CREATESTRUCTW *)lp)->lpCreateParams; app->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
            ui::dpi = GetDpiForWindow(hwnd); ui::dark(hwnd);
            auto addButton = [&](int id, const wchar_t *text) { app->controls[id - Search] = ui::button(hwnd, text, id); };
            app->controls[Search - Search] = ui::edit(hwnd, L"Find a host…", Search);
            app->controls[Quick - Search] = ui::edit(hwnd, L"user@hostname", Quick);
            SetWindowSubclass(app->control(Search), editProc, 1, (DWORD_PTR)app);
            SetWindowSubclass(app->control(Quick), editProc, 1, (DWORD_PTR)app);
            app->controls[HostList - Search] = ui::control(hwnd, L"LISTBOX", L"Saved hosts", HostList,
                LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP);
            SendMessageW(app->control(HostList), LB_SETITEMHEIGHT, 0, ui::px(65));
            SetWindowSubclass(app->control(HostList), editProc, 1, (DWORD_PTR)app);
            addButton(NewHost, L"+ New host"); addButton(ConnectHost, L"Connect →"); addButton(EditHost, L"Edit");
            addButton(QuickConnect, L"Connect →"); addButton(HomeNew, L"+ Add a host"); addButton(HomePreview, L"Color preview →");
            addButton(Advanced, L"Connection settings"); addButton(Duplicate, L"Duplicate"); addButton(Reconnect, L"Reconnect");
            addButton(SessionSettings, L"Settings"); addButton(Tools, L"Tools"); addButton(About, L"About");
            addButton(LocalCmd, L"Command Prompt"); addButton(LocalPowerShell, L"PowerShell");
            addButton(SaveCurrent, L"Save host…");
            app->refresh(); app->layout(); SetTimer(hwnd, 1, 350, nullptr); return 0;
        }
        if (!app) return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1L, r.right), std::max(1L, r.bottom));
            auto old = SelectObject(memory, bitmap); app->paint(memory);
            BitBlt(dc, 0,0,r.right,r.bottom,memory,0,0,SRCCOPY);
            SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); EndPaint(hwnd, &ps); return 0;
        }
        case WM_SIZE: if (wp != SIZE_MINIMIZED) app->layout(); return 0;
        case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize = {ui::px(980), ui::px(700)}; return 0;
        case WM_DPICHANGED: {
            ui::dpi = HIWORD(wp); auto r = (RECT *)lp;
            for (auto control : app->controls) if (control) SendMessageW(control, WM_SETFONT, (WPARAM)ui::font(), TRUE);
            SendMessageW(app->control(HostList), LB_SETITEMHEIGHT, 0, ui::px(65));
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left,r->bottom-r->top,SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: {
            COLORREF color = msg == WM_CTLCOLORLISTBOX ? ui::panel : ui::raised;
            SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, color);
            static HBRUSH panel = CreateSolidBrush(ui::panel), raised = CreateSolidBrush(ui::raised);
            return (LRESULT)(msg == WM_CTLCOLORLISTBOX ? panel : raised);
        }
        case WM_DRAWITEM: {
            auto d = (DRAWITEMSTRUCT *)lp;
            if (d->CtlID == HostList) {
                if (d->itemID >= app->filtered.size()) return TRUE;
                const auto &p = app->profiles[app->filtered[d->itemID]];
                bool selected = d->itemState & ODS_SELECTED; ui::fill(d->hDC, d->rcItem, ui::panel);
                RECT r = d->rcItem; InflateRect(&r, -ui::px(3), -ui::px(3));
                if (selected) ui::round(d->hDC, r, ui::raised, ui::line, 10);
                int y = MulDiv(r.top, 96, ui::dpi);
                ui::round(d->hDC, ui::rect(13, y + 12, 33, 33), ui::panel, selected ? ui::line : ui::panel, 8);
                ui::label(d->hDC, L">_", ui::rect(13, y + 12, 33, 33), ui::TextSize::body, ui::accent, true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                ui::label(d->hDC, p.name, ui::rect(57, y + 7, sidebar - 93, 25), ui::TextSize::body, selected ? ui::bright : ui::text, true);
                std::wstring detail = p.group.empty() ? (p.user.empty() ? L"" : p.user + L"@") + p.host : p.group + L" / " + p.host;
                ui::label(d->hDC, detail, ui::rect(57, y + 31, sidebar - 93, 22), ui::TextSize::caption, ui::muted);
                if (d->itemState & ODS_FOCUS) { RECT f = r; InflateRect(&f,-3,-3); DrawFocusRect(d->hDC,&f); }
            } else ui::drawButton(d, d->CtlID == QuickConnect || d->CtlID == ConnectHost || d->CtlID == NewHost);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == Search && HIWORD(wp) == EN_CHANGE) app->filter();
            else if (LOWORD(wp) == HostList && HIWORD(wp) == LBN_DBLCLK) app->action(ConnectHost);
            else if (HIWORD(wp) == BN_CLICKED) app->action(LOWORD(wp));
            return 0;
        case WM_CONTEXTMENU: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (p.x == -1) { RECT r; GetWindowRect(app->control(HostList), &r); p={r.left+30,r.top+30}; }
            if ((HWND)wp == app->control(HostList)) {
                POINT local = p; ScreenToClient(app->control(HostList), &local);
                auto item = SendMessageW(app->control(HostList), LB_ITEMFROMPOINT, 0, MAKELPARAM(local.x, local.y));
                if (!HIWORD(item)) SendMessageW(app->control(HostList), LB_SETCURSEL, LOWORD(item), 0);
                app->hostMenu(p);
            } else {
                POINT local=p; ScreenToClient(hwnd,&local);
                for (auto hit: app->hits) if (!hit.close && PtInRect(&hit.rect,local)) { app->tabMenu(hit.index,p); break; }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (PtInRect(&app->home,p)) { app->select(-1); return 0; }
            if (PtInRect(&app->plus,p)) { app->command(1); return 0; }
            for (auto it=app->hits.rbegin();it!=app->hits.rend();++it) if (PtInRect(&it->rect,p)) {
                int index=it->index; bool close=it->close;
                if (close) app->closeTab(index);
                else { app->dragTab=index; app->dragStart=p; app->dragging=false; SetCapture(hwnd); app->select(index); }
                break;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (app->dragTab>=0 && abs(GET_X_LPARAM(lp)-app->dragStart.x)>ui::px(8)) app->dragging=true;
            return 0;
        case WM_LBUTTONUP: {
            int from=app->dragTab; app->dragTab=-1; ReleaseCapture();
            if (from>=0 && from<(int)app->tabs.size() && app->dragging) {
                POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                for (auto hit:app->hits) if (!hit.close && PtInRect(&hit.rect,p) && hit.index!=from) {
                    auto item=std::move(app->tabs[from]); app->tabs.erase(app->tabs.begin()+from);
                    app->tabs.insert(app->tabs.begin()+hit.index,std::move(item)); app->select(hit.index); break;
                }
            }
            return 0;
        }
        case WM_MBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            for (auto hit:app->hits) if (!hit.close && PtInRect(&hit.rect,p)) { app->closeTab(hit.index); break; }
            return 0;
        }
        case WM_TIMER:
            app->poll();
#ifdef WOOK_UI_TEST
            runUiSmoke(*app);
#endif
            return 0;
        case WM_APP + 42: {
            HWND terminal=(HWND)wp; DWORD pid=0; GetWindowThreadProcessId(terminal,&pid);
            for (size_t i=0;i<app->tabs.size();++i) if (app->tabs[i]->pid==pid && pid==(DWORD)lp && GetParent(terminal)==hwnd) {
                app->tabs[i]->terminal=terminal; app->layout();
                if ((int)i==app->active) SetFocus(terminal);
                app->refresh(); break;
            }
            return 0;
        }
        case WM_APP + 43: {
            if (lp) {
                bool owned=false;
                for (auto &t:app->tabs) if (t->terminal==(HWND)lp) owned=true;
                if (!owned) return 0;
            }
            app->command((int)wp); return 0;
        }
        case WM_COPYDATA: {
            bool owned = false;
            for (auto &t : app->tabs) if (t->terminal == (HWND)wp) owned = true;
            auto *data = (const COPYDATASTRUCT *)lp;
            if (!owned || !data || data->dwData != 44 || !data->lpData || data->cbData < 2 || data->cbData > 101) return FALSE;
            auto *name = (const char *)data->lpData;
            if (name[data->cbData - 1] != '\0') return FALSE;
            auto session = wook::wide(std::string(name, data->cbData - 1));
            app->refresh();
            for (auto &profile : app->profiles) if (profile.name == session) {
                Profile copy = profile; app->connect(copy, true); return TRUE;
            }
            return FALSE;
        }
        case WM_SETFOCUS:
            if (app->active>=0 && IsWindow(app->tabs[app->active]->terminal)) SetFocus(app->tabs[app->active]->terminal);
            break;
        case WM_CLOSE: {
            bool live=false; for (auto &tab:app->tabs) if (!tab->ended && !tab->preview) live=true;
            if (live && MessageBoxW(hwnd,L"Close wShell and disconnect all active sessions?",L"Close workspace",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return 0;
            for (auto &tab:app->tabs) if (IsWindow(tab->terminal)) PostMessageW(tab->terminal,WM_CLOSE,0,0);
            DestroyWindow(hwnd); return 0;
        }
        case WM_DESTROY: KillTimer(hwnd,1); PostQuitMessage(0); return 0;
        }
    } catch (const std::exception &e) { ui::error(hwnd,e); }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
#ifdef WOOK_UI_TEST
#include "../tests/ui_smoke.inc"
#endif
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    wshellLoadFonts();
    auto command = std::wstring(commandLine);
    if (command == L"--terminal" || command.starts_with(L"--terminal ")) {
        auto arguments = wook::utf8(command);
        return wshellTerminalMain(instance, nullptr, arguments.data(), show);
    }
    try {
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        App app; app.directory=wook::executableDirectory();
#ifdef WOOK_UI_TEST
        auto isolated = app.directory + L"\\ui-data-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", isolated.c_str());
#endif
        std::wstring migrationError;
        if (!GetEnvironmentVariableW(L"WOOK_DATA_DIR", nullptr, 0)) {
            try { wook::migrateLegacySettings(app.directory + L"\\data"); }
            catch (const std::exception &error) {
                migrationError = L"Your previous data could not be fully imported. The original data folder is unchanged.\n\n" + wook::wide(error.what());
            }
        }
        wook::initializeDefaults();
        app.job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!app.job || !SetInformationJobObject(app.job,JobObjectExtendedLimitInformation,&limit,sizeof(limit)))
            throw std::runtime_error("Cannot initialize process supervision.");
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc=windowProc; wc.hInstance=instance; wc.lpszClassName=L"WookShellMain";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED);
        wc.hIconSm=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
        RegisterClassExW(&wc); ui::dpi=GetDpiForSystem();
        HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"wShell",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                                  CW_USEDEFAULT,CW_USEDEFAULT,ui::px(1230),ui::px(820),nullptr,nullptr,instance,&app);
        if (!hwnd) throw std::runtime_error("Cannot create the application window.");
        ShowWindow(hwnd,show); UpdateWindow(hwnd);
        if (!migrationError.empty()) MessageBoxW(hwnd, migrationError.c_str(), L"wShell · Data migration", MB_OK | MB_ICONEXCLAMATION);
        if (std::wstring(commandLine)==L"--preview") app.action(Preview);
        else SetFocus(app.control(Quick));
        MSG msg;
        while (GetMessageW(&msg,nullptr,0,0)>0) {
            bool consumed=false;
            if (msg.message==WM_KEYDOWN || msg.message==WM_SYSKEYDOWN) {
                bool ctrl=GetKeyState(VK_CONTROL)&0x8000, shift=GetKeyState(VK_SHIFT)&0x8000, alt=GetKeyState(VK_MENU)&0x8000;
                int command=0;
                if (ctrl && msg.wParam==VK_TAB) command=shift?6:5;
                else if (ctrl && shift && msg.wParam=='T') command=1;
                else if (ctrl && shift && msg.wParam=='D') command=2;
                else if (ctrl && shift && msg.wParam=='W') command=3;
                else if (ctrl && shift && msg.wParam=='R') command=4;
                else if (ctrl && shift && msg.wParam=='P') command=7;
                else if (ctrl && shift && msg.wParam=='L') command=10;
                else if (alt && msg.wParam>='1' && msg.wParam<='9') command=20+(int)(msg.wParam-'1');
                else if (msg.wParam==VK_F11) command=8;
                if (command) {
                    try { app.command(command); } catch (const std::exception &e) { ui::error(hwnd,e); }
                    consumed=true;
                }
            }
            if (!consumed && !IsDialogMessageW(hwnd,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        for (auto &tab:app.tabs) WaitForSingleObject(tab->process,200);
        CloseHandle(app.job);
        for (auto &tab:app.tabs) if (tab->transient) {
            wchar_t *path=wsPath(L"sessions",wook::utf8(tab->storageName).c_str()); wsRemove(path); free(path);
        }
        for (auto &[key,font]:ui::fonts) DeleteObject(font);
        return 0;
    } catch (const std::exception &e) { ui::error(nullptr,e); return 1; }
}
