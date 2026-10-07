#include "core.hpp"
#include "dialog.hpp"
#include "ui.hpp"
#include "resources.hpp"
#include "key_dialog.hpp"
#include "backup.hpp"
#include "credentials.h"
#include "sftp_view.hpp"
#include "split.hpp"
#include "broadcast.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef WOOK_UI_TEST
#include "../tests/capture.hpp"
#include "keys.h"
#include "ime.h"
extern "C" int mk_wcwidth(unsigned int);
#endif

using wook::Profile;
extern "C" int WINAPI wshellTerminalMain(HINSTANCE, HINSTANCE, LPSTR, int);
namespace {
enum { Search = 100, HostList, NewHost, ConnectHost, EditHost, Quick, QuickConnect, Preview,
       Advanced, Duplicate, Reconnect, SessionSettings, Tools, About, SaveCurrent, HomeNew, HomePreview, LocalCmd, LocalPowerShell, FilesHost, FilesSession, Split, CommandText, SendCommand, SendAll, SyncInput, Targets, NavigationToggle, ControlEnd };
constexpr int sidebarExpanded = 252, appBarHeight = 48;
struct Tab {
    Profile profile;
    std::wstring storageName;
    HWND terminal = nullptr;
    HANDLE process = nullptr;
    DWORD pid = 0;
    bool preview = false, transient = false, closing = false, ended = false, files = false;
    ~Tab() { if (process) CloseHandle(process); }
};
struct Hit { RECT rect; int index; bool close; };
struct App {
    HWND hwnd = nullptr, controls[ControlEnd - Search]{}, tooltips = nullptr;
    std::wstring tooltipText;
    bool navigationVisible = true;
    int sidebar = sidebarExpanded;
    HANDLE job = nullptr;
    std::wstring directory;
    std::vector<Profile> profiles;
    std::vector<size_t> filtered;
    std::vector<std::unique_ptr<Tab>> tabs;
    std::vector<Hit> hits;
    std::vector<Tab *> panes;
    std::vector<wook::PaneRect> paneRects;
    int splitCount = 1;
    std::vector<Tab *> routingPanes;
    std::vector<Tab *> excludedPanes;
    bool paneZoom = false;
    HWND keyboardPane = nullptr;
    bool panePulseBright = true;
    ULONGLONG paneFocusTime = 0;
    int toolbarLeft = 0;
    std::wstring commandStatus = L"Send → active pane";
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
    void setNavigation(bool show);
    void hostActions();
    void select(int index);
    void focusActive();
    void refreshPaneFocus();
    void connect(const Profile &profile, bool saved, bool preview = false, bool advanced = false, bool files = false);
    void localShell(bool powershell = false);
    void closeTab(int index);
    void command(int code);
    void action(int id);
    void poll();
    void toolsMenu();
    void hostMenu(POINT point);
    void tabMenu(int index, POINT point);
    void splitMenu();
    void setSplit(int count);
    void zoomPane();
    void movePane(int direction);
    void targetsMenu();
    void toggleTarget(Tab *tab);
    bool isTarget(Tab *tab) const;
    void inputStatus();
    bool inputReady(Tab *tab);
    void sendCommand();
    void relayInput(HWND source, const COPYDATASTRUCT *data);
    Profile *selectedHost();
};
#ifdef WOOK_UI_TEST
void runUiSmoke(App &app);
#endif
constexpr int paneHeader = 44, paneInset = 3;
COLORREF hostColor(const Profile &p) {
    if (p.tabColor.size() != 6 || p.tabColor.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return ui::accent;
    auto n = wcstoul(p.tabColor.c_str(), nullptr, 16); return RGB((n >> 16) & 255, (n >> 8) & 255, n & 255);
}
void App::focusActive() {
    if (active >= 0 && IsWindow(tabs[active]->terminal) && IsWindowVisible(tabs[active]->terminal))
        SetFocus(tabs[active]->terminal);
    refreshPaneFocus();
}
void App::refreshPaneFocus() {
    HWND focused = nullptr;
    GUITHREADINFO info{sizeof(info)};
    if (active >= 0 && GetForegroundWindow() == hwnd && GetGUIThreadInfo(0, &info) &&
        !(info.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE))) {
        for (size_t i = 0; i < tabs.size(); ++i) {
            auto *tab = tabs[i].get();
            if (!IsWindow(tab->terminal) || !IsWindowVisible(tab->terminal) ||
                std::find(panes.begin(), panes.end(), tab) == panes.end()) continue;
            if (info.hwndFocus != tab->terminal && !(tab->files && IsChild(tab->terminal, info.hwndFocus))) continue;
            focused = tab->terminal;
            if (active != (int)i) {
                active = (int)i; inputStatus(); layout();
            }
            break;
        }
    }
    auto now = GetTickCount64();
    bool changed = focused != keyboardPane;
    if (changed) { keyboardPane = focused; paneFocusTime = now; }
    // Keep the orange frame visible in both phases; only the keyboard owner
    // pulses. A selected command destination is not necessarily focused.
    UINT blinkTime = GetCaretBlinkTime();
    bool bright = !focused || !blinkTime || blinkTime == INFINITE || ((now - paneFocusTime) / 650) % 2 == 0;
    if (changed || bright != panePulseBright) {
        panePulseBright = bright;
        if (active >= 0 && panes.size() > 1) for (const auto &r : paneRects) {
            if (r.width <= 0) continue;
            RECT frames[] = {ui::rect(r.x, r.y, r.width, paneHeader),
                ui::rect(r.x, r.y + paneHeader, paneInset, r.height - paneHeader),
                ui::rect(r.x + r.width - paneInset, r.y + paneHeader, paneInset, r.height - paneHeader),
                ui::rect(r.x, r.y + r.height - paneInset, r.width, paneInset)};
            for (const auto &frame : frames) InvalidateRect(hwnd, &frame, FALSE);
        }
    }
}
bool App::inputReady(Tab *tab) {
    if (!tab || tab->ended || tab->closing || tab->files || tab->preview || !IsWindow(tab->terminal)) return false;
    DWORD_PTR ready = 0;
    return SendMessageTimeoutW(tab->terminal, WM_APP + 61, 0, 0, SMTO_ABORTIFHUNG, 100, &ready) && ready;
}
bool App::isTarget(Tab *tab) const {
    return tab && !tab->files && !tab->preview && !tab->ended && !tab->closing &&
        std::find(excludedPanes.begin(), excludedPanes.end(), tab) == excludedPanes.end();
}
void App::inputStatus() {
    bool sync = SendMessageW(control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool all = SendMessageW(control(SendAll), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int count = 0; for (auto *tab : panes) if (isTarget(tab) && inputReady(tab)) ++count;
    commandStatus = sync ? L"LIVE · " + std::to_wstring(count) + L" ready · type in a target pane" :
        all ? L"Send → " + std::to_wstring(count) + L" ready targets" : L"Send → active pane";
    if (sync && active >= 0 && !isTarget(tabs[active].get())) commandStatus = L"LIVE paused · active pane excluded";
}
void App::toggleTarget(Tab *tab) {
    auto it = std::find(excludedPanes.begin(), excludedPanes.end(), tab);
    if (it == excludedPanes.end()) excludedPanes.push_back(tab); else excludedPanes.erase(it);
    inputStatus(); layout();
}
void App::targetsMenu() {
    auto menuPanes = panes;
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < panes.size(); ++i) {
        auto *tab = panes[i];
        auto name = std::to_wstring(i + 1) + L"  " + tab->profile.displayName();
        // Menu labels must not interpret a host alias as a mnemonic.
        for (size_t p = 0; (p = name.find(L'&', p)) != std::wstring::npos; p += 2) name.insert(p, 1, L'&');
        bool available = !tab->files && !tab->preview && !tab->ended && !tab->closing;
        AppendMenuW(menu, MF_STRING | (isTarget(tab) ? MF_CHECKED : 0) | (available ? 0 : MF_GRAYED), i + 1, name.c_str());
    }
    RECT r; GetWindowRect(control(Targets), &r);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.top, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (panes != menuPanes) return; // A connection can close while the menu's message loop is running.
    if (choice > 0 && choice <= (int)panes.size()) toggleTarget(panes[choice - 1]);
}
void App::zoomPane() {
    if (active < 0 || panes.size() < 2) return;
    paneZoom = !paneZoom; routingPanes.clear(); layout();
    focusActive();
}
void App::movePane(int direction) {
    if (active < 0 || panes.size() < 2 || paneZoom) return;
    auto it = std::find(panes.begin(), panes.end(), tabs[active].get());
    int next = wook::adjacentPane(paneRects, (int)(it - panes.begin()), direction);
    if (next < 0 || next >= (int)panes.size()) return;
    for (size_t i = 0; i < tabs.size(); ++i) if (tabs[i].get() == panes[next]) { select((int)i); break; }
}
void App::sendCommand() {
    if (active < 0 || panes.size() < 2 || paneZoom) return;
    auto text = ui::value(control(CommandText)); if (text.empty()) return;
    if (text.find_first_of(L"\r\n") != std::wstring::npos) throw std::runtime_error("Send one command line at a time.");
    text += L'\r';
    std::vector<unsigned char> bytes(sizeof(WsInputHeader) + text.size() * sizeof(wchar_t));
    WsInputHeader header{1, 0, (int)text.size()}; memcpy(bytes.data(), &header, sizeof(header));
    memcpy(bytes.data() + sizeof(header), text.data(), text.size() * sizeof(wchar_t));
    COPYDATASTRUCT packet{WSHELL_INPUT_MESSAGE, (DWORD)bytes.size(), bytes.data()};
    bool all = SendMessageW(control(SendAll), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int sent = 0, skipped = 0;
    for (auto *tab : panes) if (all ? isTarget(tab) : tab == tabs[active].get()) {
        DWORD_PTR accepted = 0;
        if (inputReady(tab) && SendMessageTimeoutW(tab->terminal, WM_COPYDATA, (WPARAM)hwnd, (LPARAM)&packet, SMTO_ABORTIFHUNG, 500, &accepted) && accepted) ++sent;
        else ++skipped;
    }
    SecureZeroMemory(bytes.data(), bytes.size()); SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
    commandStatus = L"Sent to " + std::to_wstring(sent) + L" terminal(s)" + (skipped ? L" · " + std::to_wstring(skipped) + L" unavailable / skipped" : L"");
    if (sent) SetWindowTextW(control(CommandText), L"");
    InvalidateRect(hwnd, nullptr, FALSE);
}
void App::relayInput(HWND source, const COPYDATASTRUCT *data) {
    if (active < 0 || paneZoom || panes.size() < 2 || SendMessageW(control(SyncInput), BM_GETCHECK, 0, 0) != BST_CHECKED ||
        !data || !wsInputValid(data->lpData, data->cbData)) return;
    // Sent input can overtake the child's posted focus notification. Verify the
    // actual focus before updating selection, so the first keystroke is not lost.
    if (tabs[active]->terminal != source) {
        GUITHREADINFO info{sizeof(info)};
        if (!GetGUIThreadInfo(GetWindowThreadProcessId(source, nullptr), &info) || info.hwndFocus != source) return;
        for (size_t i = 0; i < tabs.size(); ++i) if (tabs[i]->terminal == source &&
            std::find(panes.begin(), panes.end(), tabs[i].get()) != panes.end()) {
            active = (int)i; inputStatus(); layout(); break;
        }
    }
    if (tabs[active]->terminal != source || !isTarget(tabs[active].get()) || !inputReady(tabs[active].get())) return;
    if (std::find(panes.begin(), panes.end(), tabs[active].get()) == panes.end()) return;
    int sent = 1, skipped = 0;
    for (auto *tab : panes) if (tab->terminal != source && isTarget(tab)) {
        DWORD_PTR accepted = 0;
        if (inputReady(tab) && SendMessageTimeoutW(tab->terminal, WM_COPYDATA, (WPARAM)hwnd, (LPARAM)data, SMTO_ABORTIFHUNG, 500, &accepted) && accepted) ++sent;
        else ++skipped;
    }
    commandStatus = L"LIVE → " + std::to_wstring(sent) + L" terminal(s)" + (skipped ? L" · " + std::to_wstring(skipped) + L" skipped" : L"");
    RECT status = ui::rect(sidebar, height - 65, width - sidebar, 30); InvalidateRect(hwnd, &status, FALSE);
}
void visible(HWND hwnd, bool show) { ShowWindow(hwnd, show ? SW_SHOWNA : SW_HIDE); }
std::wstring lower(std::wstring text) { std::transform(text.begin(), text.end(), text.begin(), towlower); return text; }
void App::setNavigation(bool show) {
    if (navigationVisible == show) return;
    wook::saveNavigationVisible(show);
    auto focus = GetFocus();
    bool restoreFocus = focus == control(NavigationToggle) || (!show && (focus == control(Search) || focus == control(HostList)));
    navigationVisible = show; sidebar = show ? sidebarExpanded : 0;
    layout();
    if (restoreFocus) { if (active < 0) SetFocus(control(Quick)); else focusActive(); }
}
void App::hostActions() {
    auto selected = selectedHost();
    EnableWindow(control(ConnectHost), selected != nullptr);
    EnableWindow(control(EditHost), selected != nullptr);
    EnableWindow(control(FilesHost), selected && selected->protocol == L"ssh");
    InvalidateRect(hwnd, nullptr, FALSE);
}
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
        if (!query.empty() && lower(p.name + L" " + p.alias + L" " + p.host + L" " + p.group + L" " + p.user).find(query) == std::wstring::npos) continue;
        if (p.name == old) selected = (int)filtered.size();
        filtered.push_back(i);
        SendMessageW(control(HostList), LB_ADDSTRING, 0, (LPARAM)p.name.c_str());
    }
    if (!filtered.empty()) SendMessageW(control(HostList), LB_SETCURSEL, selected, 0);
    SendMessageW(control(HostList), WM_SETREDRAW, TRUE, 0);
    InvalidateRect(control(HostList), nullptr, TRUE);
    hostActions();
    visible(control(HostList), navigationVisible && !filtered.empty());
}
void App::refresh() { auto next = wook::loadProfiles(); filtered.clear(); profiles = std::move(next); filter(); }
Profile *App::selectedHost() {
    auto i = SendMessageW(control(HostList), LB_GETCURSEL, 0, 0);
    return i >= 0 && (size_t)i < filtered.size() ? &profiles[filtered[i]] : nullptr;
}
void App::layout() {
    RECT client; GetClientRect(hwnd, &client);
    width = MulDiv(client.right, 96, ui::dpi); height = MulDiv(client.bottom, 96, ui::dpi);
    ui::place(control(NavigationToggle), 130, 8, 112, 32);
    SetWindowTextW(control(NavigationToggle), navigationVisible ? L"Hide hosts" : L"Show hosts");
    ui::place(control(NewHost), 250, 8, 102, 32);
    ui::place(control(ConnectHost), 360, 8, 82, 32);
    ui::place(control(FilesHost), 450, 8, 66, 32);
    ui::place(control(EditHost), 524, 8, 62, 32);
    ui::place(control(Advanced), 594, 8, 156, 32);
    ui::place(control(Tools), 758, 8, 80, 32);
    ui::place(control(About), 846, 8, 70, 32);
    ui::place(control(Search), 26, 112, sidebarExpanded - 52, 22);
    ui::place(control(HostList), 10, 158, sidebarExpanded - 20, std::max(55, height - 196));
    visible(control(Search), navigationVisible);
    visible(control(HostList), navigationVisible && !filtered.empty());
    int left = sidebar + 48, span = std::max(360, width - left - 48), card = (span - 17) / 2;
    ui::place(control(Quick), left + 17, appBarHeight + 246, std::max(100, span - 166), 25);
    ui::place(control(QuickConnect), left + span - 123, appBarHeight + 235, 123, 46);
    ui::place(control(HomeNew), left + 22, appBarHeight + 437, card - 44, 40);
    ui::place(control(HomePreview), left + card + 39, appBarHeight + 437, card - 44, 40);
    ui::place(control(LocalCmd), left + span - 336, appBarHeight + 536, 166, 40);
    ui::place(control(LocalPowerShell), left + span - 160, appBarHeight + 536, 138, 40);
    ui::place(control(Duplicate), width - 330, appBarHeight + 64, 95, 33);
    ui::place(control(Split), width - 434, appBarHeight + 64, 95, 33);
    ui::place(control(FilesSession), width - 522, appBarHeight + 64, 80, 33);
    bool showFiles = active >= 0 && !tabs[active]->preview && !tabs[active]->files && tabs[active]->profile.protocol == L"ssh";
    toolbarLeft = width - (showFiles ? 522 : 434);
    ui::place(control(Reconnect), width - 226, appBarHeight + 64, 102, 33);
    ui::place(control(SessionSettings), width - 115, appBarHeight + 64, 95, 33);
    ui::place(control(SaveCurrent), width - 126, height - 30, 116, 27);
    for (int id : {Quick, QuickConnect, HomeNew, HomePreview, LocalCmd, LocalPowerShell}) visible(control(id), active < 0);
    for (int id : {Duplicate, Reconnect, SessionSettings}) visible(control(id), active >= 0);
    visible(control(Split), active >= 0);
    visible(control(SaveCurrent), active >= 0 && !tabs[active]->preview && tabs[active]->transient && tabs[active]->profile.protocol != L"local");
    visible(control(FilesSession), showFiles);
    EnableWindow(control(SessionSettings), active >= 0 && !tabs[active]->files && tabs[active]->profile.protocol != L"local");
    panes.erase(std::remove_if(panes.begin(), panes.end(), [&](Tab *p) {
        return std::none_of(tabs.begin(), tabs.end(), [&](const auto &t) { return t.get() == p; });
    }), panes.end());
    if (active >= 0) {
        if (splitCount == 1) panes = {tabs[active].get()};
        else {
            if (panes.empty()) panes.push_back(tabs[active].get());
            for (auto &t : tabs) if ((int)panes.size() < splitCount && std::find(panes.begin(), panes.end(), t.get()) == panes.end()) panes.push_back(t.get());
        }
    }
    if (active < 0 || panes.size() < 2) paneZoom = false;
    bool commandBar = active >= 0 && panes.size() > 1 && !paneZoom;
    if (routingPanes != panes || !commandBar) {
        SendMessageW(control(SendAll), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(control(SyncInput), BM_SETCHECK, BST_UNCHECKED, 0);
        commandStatus = L"Send → active pane"; routingPanes = panes; excludedPanes.clear();
    }
    for (int id : {CommandText, SendCommand, SendAll, SyncInput, Targets}) visible(control(id), commandBar);
    ui::place(control(CommandText), sidebar+18, height-133, width-sidebar-132, 26);
    ui::place(control(SendCommand), width-104, height-141, 88, 37);
    ui::place(control(SendAll), sidebar+16, height-101, 190, 26);
    ui::place(control(SyncInput), sidebar+218, height-101, 180, 26);
    ui::place(control(Targets), width-172, height-102, 156, 28);
    int targets = 0; for (auto *tab : panes) if (isTarget(tab)) ++targets;
    SetWindowTextW(control(Targets), (L"Targets: " + std::to_wstring(targets) + L" / " + std::to_wstring(panes.size())).c_str());
    paneRects = wook::splitRects(std::max(1, (int)panes.size()), sidebar + 1, appBarHeight + 113, width - sidebar - 1, std::max(40, height - appBarHeight - 148 - (commandBar ? 124 : 0)));
    if (paneZoom) for (size_t i = 0; i < panes.size(); ++i) paneRects[i] = panes[i] == tabs[active].get() ?
        wook::PaneRect{sidebar+1,appBarHeight+113,width-sidebar-1,height-appBarHeight-148} : wook::PaneRect{};
    for (size_t i = 0; i < tabs.size(); ++i) {
        auto &tab = *tabs[i];
        if (IsWindow(tab.terminal)) {
            if (commandBar && SendMessageW(control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED && isTarget(&tab) && std::find(panes.begin(), panes.end(), &tab) != panes.end())
                SetPropW(tab.terminal, L"wShell.SyncInput", (HANDLE)1);
            else RemovePropW(tab.terminal, L"wShell.SyncInput");
            auto pane = std::find(panes.begin(), panes.end(), &tab);
            if (active >= 0 && pane != panes.end() && (!paneZoom || (int)i == active)) {
                auto r = paneRects[pane - panes.begin()];
                int inset = panes.size() > 1 ? paneInset : 0, header = panes.size() > 1 ? paneHeader : 0;
                ui::place(tab.terminal, r.x + inset, r.y + header, r.width - inset * 2, std::max(10, r.height - header - inset));
                ShowWindow(tab.terminal, SW_SHOWNA);
            } else ShowWindow(tab.terminal, SW_HIDE);
        }
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}
void App::select(int index) {
    int next = std::clamp(index, -1, (int)tabs.size() - 1);
    if (next >= 0 && splitCount > 1 && std::find(panes.begin(), panes.end(), tabs[next].get()) == panes.end()) {
        paneZoom = false;
        auto old = active >= 0 ? std::find(panes.begin(), panes.end(), tabs[active].get()) : panes.end();
        if (old != panes.end()) *old = tabs[next].get();
        else if (!panes.empty()) panes[0] = tabs[next].get();
    }
    active = next;
    if (active >= 0 && active < tabScroll) tabScroll = active;
    int capacity = std::max(1, (width - sidebar - 162) / 160);
    if (active >= tabScroll + capacity) tabScroll = active - capacity + 1;
    tabScroll = std::clamp(tabScroll, 0, std::max(0, (int)tabs.size() - 1));
    layout();
    if (active >= 0 && panes.size() > 1 && !paneZoom) inputStatus();
    if (active < 0) SetFocus(control(Quick));
    focusActive();
}
void App::setSplit(int count) {
    if (active < 0 || count < 1 || count > 4 || count > (int)tabs.size()) return;
    splitCount = count; panes = {tabs[active].get()};
    paneZoom = false;
    for (auto &t : tabs) if ((int)panes.size() < count && t.get() != tabs[active].get()) panes.push_back(t.get());
    layout();
    focusActive();
}
void App::splitMenu() {
    HMENU menu = CreatePopupMenu();
    const wchar_t *labels[] = {L"Single pane", L"2 panes — side by side", L"3 panes — left + stacked right", L"4 panes — grid"};
    for (int i = 1; i <= 4; ++i) AppendMenuW(menu, MF_STRING | (splitCount == i ? MF_CHECKED : 0) | ((int)tabs.size() < i ? MF_GRAYED : 0), i, labels[i - 1]);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (panes.size() < 2 ? MF_GRAYED : 0), 5, paneZoom ? L"Restore split\tCtrl+Shift+Enter" : L"Zoom active pane\tCtrl+Shift+Enter");
    AppendMenuW(menu, MF_STRING | (panes.size() < 2 || paneZoom ? MF_GRAYED : 0), 6, L"Sync keyboard\tCtrl+Shift+B");
    AppendMenuW(menu, MF_STRING | (panes.size() < 2 || paneZoom ? MF_GRAYED : 0), 7, L"Focus command bar\tCtrl+Shift+K");
    RECT r; GetWindowRect(control(Split), &r);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (choice >= 1 && choice <= 4) setSplit(choice);
    else if (choice >= 5 && choice <= 7) command(choice + 7);
}
void App::paint(HDC dc) {
    ui::fill(dc, ui::rect(0, 0, width, height), ui::bg);
    ui::fill(dc, ui::rect(0, 0, width, appBarHeight), ui::panel);
    ui::fill(dc, ui::rect(0, appBarHeight - 1, width, 1), ui::line);
    ui::label(dc, L"wShell", ui::rect(18, 5, 104, 37), ui::TextSize::title, ui::bright, true);
    if (auto p = selectedHost(); p && width >= 1090)
        ui::label(dc, L"Host: " + p->displayName(), ui::rect(934, 8, width - 950, 32), ui::TextSize::caption, ui::muted);
    if (navigationVisible) {
        ui::fill(dc, ui::rect(0, appBarHeight, sidebar, height - appBarHeight), ui::panel);
        ui::fill(dc, ui::rect(sidebar, appBarHeight, 1, height - appBarHeight), ui::line);
        ui::label(dc, L"CONNECTIONS", ui::rect(20, 66, 160, 23), ui::TextSize::caption, ui::muted, true);
        auto count = ui::value(control(Search)).empty() ? std::to_wstring(profiles.size()) : std::to_wstring(filtered.size()) + L" / " + std::to_wstring(profiles.size());
        ui::label(dc, count, ui::rect(sidebar - 78, 66, 58, 23), ui::TextSize::caption, ui::muted, false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        ui::round(dc, ui::rect(18, 102, sidebar - 36, 40), ui::raised);
        if (filtered.empty()) {
            ui::label(dc, profiles.empty() ? L"No saved connections." : L"No matching connections.", ui::rect(24, 172, 210, 25), ui::TextSize::body, ui::muted);
            ui::label(dc, profiles.empty() ? L"Use New host to add one." : L"Try another name or address.", ui::rect(24, 200, 210, 22), ui::TextSize::caption, ui::muted);
        }
    }
    ui::fill(dc, ui::rect(sidebar + 1, appBarHeight + 51, width - sidebar, 1), ui::line);
    hits.clear(); home = ui::rect(sidebar + 9, appBarHeight + 9, 96, 35);
    if (active < 0) ui::round(dc, home, ui::raised, ui::line, 8);
    ui::label(dc, L"Workspace", home, ui::TextSize::body, active < 0 ? ui::bright : ui::muted, active < 0, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    int x = sidebar + 112, available = width - x - 51;
    int capacity = std::max(1, available / 160);
    int tabWidth = std::min(198, available / std::max(1, std::min(capacity, (int)tabs.size())));
    for (int i = tabScroll; i < (int)tabs.size() && i < tabScroll + capacity; ++i) {
        RECT r = ui::rect(x, appBarHeight + 9, tabWidth - 5, 35);
        if (i == active) {
            ui::round(dc, r, ui::raised, ui::line, 8);
            ui::fill(dc, ui::rect(x + 14, appBarHeight + 43, tabWidth - 33, 2), ui::accent);
        }
        auto &tab = *tabs[i];
        ui::fill(dc, ui::rect(x+5, appBarHeight + 16, 3, 21), hostColor(tab.profile));
        ui::label(dc, tab.preview ? L"Color preview" : (tab.files ? L"SFTP · " : L"") + tab.profile.displayName(), ui::rect(x + 13, appBarHeight + 9, tabWidth - 49, 35), ui::TextSize::caption, i == active ? ui::bright : ui::muted);
        RECT cross = ui::rect(x + tabWidth - 33, appBarHeight + 14, 24, 25);
        ui::label(dc, L"×", cross, ui::TextSize::section, ui::muted, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        hits.push_back({r, i, false}); hits.push_back({cross, i, true}); x += tabWidth;
    }
    plus = ui::rect(width - 40, appBarHeight + 11, 30, 31);
    ui::label(dc, L"+", plus, ui::TextSize::title, ui::muted, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (active < 0) {
        int left = sidebar + 48, span = std::max(360, width - left - 48);
        ui::label(dc, L"LESS FRICTION. MORE FLOW.", ui::rect(left, appBarHeight + 102, span, 24), ui::TextSize::caption, ui::accent, true);
        ui::label(dc, L"Your servers. One quiet workspace.", ui::rect(left, appBarHeight + 138, span, 50), ui::TextSize::title, ui::bright, true);
        ui::label(dc, L"A familiar terminal, with room for every connection.", ui::rect(left, appBarHeight + 192, span, 29), ui::TextSize::body, ui::muted);
        ui::round(dc, ui::rect(left, appBarHeight + 235, span - 134, 46), ui::raised, ui::line, 12);
        ui::label(dc, L"QUICK CONNECT", ui::rect(left, appBarHeight + 299, 140, 23), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"user@hostname:22   or   ssh://user@[::1]:22", ui::rect(left + 139, appBarHeight + 299, span - 139, 23), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int gap = 17, card = (span - gap) / 2;
        ui::round(dc, ui::rect(left, appBarHeight + 348, card, 145), ui::panel, ui::raised, 16);
        ui::round(dc, ui::rect(left + card + gap, appBarHeight + 348, card, 145), ui::panel, ui::raised, 16);
        ui::label(dc, L"A home for every host", ui::rect(left + 22, appBarHeight + 365, card - 44, 30), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Save it once. Open it in a new tab.", ui::rect(left + 22, appBarHeight + 402, card - 44, 24), ui::TextSize::body, ui::muted);
        ui::label(dc, L"Made for the command line", ui::rect(left + card + gap + 22, appBarHeight + 365, card - 44, 30), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Warm colors. Sharp type. Full color.", ui::rect(left + card + gap + 22, appBarHeight + 402, card - 44, 24), ui::TextSize::body, ui::muted);
        ui::round(dc, ui::rect(left, appBarHeight + 511, span, 84), ui::panel, ui::raised, 16);
        ui::label(dc, L"Local terminal", ui::rect(left + 22, appBarHeight + 526, span - 376, 27), ui::TextSize::section, ui::bright, true);
        ui::label(dc, L"Work on this computer.", ui::rect(left + 22, appBarHeight + 558, span - 376, 22), ui::TextSize::body, ui::muted);
        if (height >= appBarHeight + 738) {
        int y = std::max(appBarHeight + 620, height - 142);
        ui::label(dc, L"BUILT TO STAY OUT OF YOUR WAY", ui::rect(left, y, span, 21), ui::TextSize::caption, ui::muted, true);
        ui::label(dc, L"SSH  /  Local shell  /  Serial     ·     Tabs that travel with you", ui::rect(left, y + 32, span, 26), ui::TextSize::body, ui::text);
        ui::label(dc, L"Ctrl + Shift + T   new connection       Ctrl + Tab   switch tabs", ui::rect(left, y + 66, span, 22), ui::TextSize::caption, ui::muted, false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        auto &tab = *tabs[active];
        int titleWidth = std::max(40, toolbarLeft - sidebar - 34);
        ui::label(dc, tab.preview ? L"Terminal preview" : tab.profile.displayName(), ui::rect(sidebar + 22, appBarHeight + 61, titleWidth, 24), ui::TextSize::body, ui::bright, true);
        std::wstring endpoint = tab.preview ? L"Local preview · no connection" : tab.profile.protocol == L"local" ? L"Local terminal · this computer" : tab.profile.protocol + L"  /  " + (tab.profile.user.empty() ? L"" : tab.profile.user + L"@") + tab.profile.host + L":" + std::to_wstring(tab.profile.port);
        ui::label(dc, endpoint, ui::rect(sidebar + 22, appBarHeight + 85, titleWidth, 19), ui::TextSize::caption, ui::muted);
        ui::fill(dc, ui::rect(sidebar, appBarHeight + 112, width - sidebar, 1), ui::raised);
        if (panes.size() > 1) {
            for (size_t i = 0; i < panes.size(); ++i) {
                auto r = paneRects[i]; bool focused = panes[i] == &tab;
                if (r.width == 0) continue;
                bool typing = focused && keyboardPane && keyboardPane == panes[i]->terminal;
                COLORREF frame = !focused ? ui::line : typing && panePulseBright ? RGB(255, 164, 82) : ui::accent;
                ui::fill(dc, ui::rect(r.x, r.y, r.width, r.height), frame);
                ui::fill(dc, ui::rect(r.x+paneInset, r.y+paneInset, r.width-paneInset*2, r.height-paneInset*2), ui::bg);
                ui::fill(dc, ui::rect(r.x+paneInset, r.y+paneInset, r.width-paneInset*2, paneHeader-paneInset), focused ? ui::raised : ui::panel);
                ui::fill(dc, ui::rect(r.x+9,r.y+9,3,26), hostColor(panes[i]->profile));
                ui::label(dc, std::to_wstring(i+1) + L"  " + panes[i]->profile.displayName(),
                    ui::rect(r.x+18,r.y+4,r.width-86,20), ui::TextSize::caption, focused ? ui::bright : ui::text, focused);
                bool sync = SendMessageW(control(SyncInput), BM_GETCHECK, 0, 0) == BST_CHECKED;
                std::wstring state = focused ? (typing ? L"ACTIVE" : L"SELECTED") : L"INACTIVE";
                state += panes[i]->files ? L" · SFTP" : panes[i]->preview ? L" · PREVIEW" : panes[i]->ended ? L" · ENDED" :
                    !isTarget(panes[i]) ? L" · EXCLUDED" : sync ? L" · SYNC ON" : L" · SYNC OFF";
                ui::label(dc, state, ui::rect(r.x+18,r.y+23,r.width-86,17), ui::TextSize::caption, focused || (sync && isTarget(panes[i])) ? ui::accent : ui::muted, focused);
                auto zoom = ui::rect(r.x+r.width-62,r.y+8,53,28);
                ui::round(dc, zoom, ui::raised);
                ui::label(dc, paneZoom ? L"Back" : L"Zoom", zoom, ui::TextSize::caption, ui::text, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (!IsWindow(panes[i]->terminal)) ui::label(dc, panes[i]->ended ? L"Session ended · Reconnect" : L"Connecting…", ui::rect(r.x+12,r.y+40,r.width-24,28), ui::TextSize::body, ui::muted);
            }
        } else if (!IsWindow(tab.terminal)) {
            int cx = sidebar + 56;
            ui::label(dc, tab.ended ? L"This session has ended." : tab.closing ? L"Closing session…" : L"Preparing your terminal…", ui::rect(cx, appBarHeight + 220, width - cx - 40, 50), ui::TextSize::title, ui::bright, true);
            ui::label(dc, tab.ended ? L"Reconnect to start again, or open another host." : L"Complete any connection or configuration dialog to continue.", ui::rect(cx, appBarHeight + 279, width - cx - 40, 30), ui::TextSize::body, ui::muted);
        }
    }
    if (active >= 0 && panes.size() > 1 && !paneZoom) {
        ui::round(dc, ui::rect(sidebar+12,height-143,width-sidebar-125,42), ui::raised);
        ui::label(dc, commandStatus, ui::rect(sidebar+18,height-68,width-sidebar-36,28), ui::TextSize::caption, ui::accent);
    }
    ui::fill(dc, ui::rect(0, height - 32, width, 32), ui::panel);
    ui::fill(dc, ui::rect(0, height - 33, width, 1), ui::line);
    if (navigationVisible) {
        ui::label(dc, L"●  LOCAL DATA", ui::rect(20, height - 30, 125, 26), ui::TextSize::caption, ui::accent, true);
        ui::label(dc, std::to_wstring(tabs.size()) + L" tabs", ui::rect(158, height - 30, 75, 26), ui::TextSize::caption, ui::muted);
    }
    ui::label(dc, L"Flexoki Dark   /   JetBrains Mono", ui::rect(sidebar + 20, height - 30, 300, 26), ui::TextSize::caption, ui::muted);
    if (active < 0 || !tabs[active]->transient || tabs[active]->preview || tabs[active]->profile.protocol == L"local")
        ui::label(dc, L"NATIVE  ·  WINDOWS x64", ui::rect(width - 200, height - 30, 180, 26), ui::TextSize::caption, ui::muted, false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}
void App::connect(const Profile &profile, bool saved, bool preview, bool advanced, bool files) {
    if (files && profile.protocol != L"ssh") throw std::runtime_error("SFTP requires an SSH host.");
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
        }
        bool ok = wsSave(store); wsClose(store);
        if (!ok) throw std::runtime_error("Cannot save temporary session settings.");
    }
    if (files) {
        tab->files = true;
        tab->terminal = createSftpView(hwnd, job, tab->storageName, saved);
        tabs.push_back(std::move(tab)); select((int)tabs.size() - 1); return;
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
    if (tab.files || tab.ended || WaitForSingleObject(tab.process, 0) == WAIT_OBJECT_0) {
        if (tab.files && IsWindow(tab.terminal)) DestroyWindow(tab.terminal);
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
        if (!tab.files && !tab.ended && WaitForSingleObject(tab.process, 0) == WAIT_OBJECT_0) {
            tab.ended = true; tab.terminal = nullptr;
            routingPanes.clear();
            if (tab.closing) { closeTab(i); continue; }
            refresh(); layout();
        }
    }
    refreshPaneFocus();
}
void App::command(int code) {
    if (code >= 30 && code <= 33) { movePane(code - 30); return; }
    if (code >= 20 && code < 29) { select(code - 21); return; }
    switch (code) {
    case 1: select(-1); SetFocus(control(Quick)); break;
    case 2:
        if (active >= 0) { auto &t = *tabs[active]; connect(t.profile, !t.transient, t.preview, false, t.files); } break;
    case 3: closeTab(active); break;
    case 4:
        if (active >= 0) {
            routingPanes.clear(); layout();
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
    case 7: setNavigation(true); SetFocus(control(Search)); SendMessageW(control(Search), EM_SETSEL, 0, -1); break;
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
    case 11: splitMenu(); break;
    case 12: zoomPane(); break;
    case 13:
        if (active >= 0 && panes.size() > 1 && !paneZoom) SendMessageW(control(SyncInput), BM_CLICK, 0, 0);
        break;
    case 14:
        if (active >= 0 && panes.size() > 1 && !paneZoom) SetFocus(control(CommandText));
        break;
    case 15: setNavigation(!navigationVisible); break;
    }
}
void App::action(int id) {
    if (id == SendCommand) { sendCommand(); return; }
    if (id == SendAll || id == SyncInput) {
        inputStatus(); layout();
        if (id == SyncInput) focusActive();
        else if (id == SendAll) SetFocus(control(CommandText));
        return;
    }
    switch (id) {
    case NavigationToggle: command(15); break;
    case Split: splitMenu(); break;
    case Targets: targetsMenu(); break;
    case NewHost: case HomeNew: { Profile p; if (editHost(hwnd, p, false)) refresh(); break; }
    case EditHost: {
        auto selected = selectedHost(); if (!selected) break;
        Profile p = *selected; auto oldName = p.name;
        if (editHost(hwnd, p, true)) {
            for (auto &tab : tabs) if (tab->profile.name == oldName && !tab->transient) { tab->profile.alias = p.alias; tab->profile.tabColor = p.tabColor; }
            refresh(); layout();
        }
        break;
    }
    case ConnectHost: if (auto p = selectedHost()) connect(*p, true); break;
    case FilesHost: if (auto p = selectedHost()) connect(*p, true, false, false, true); break;
    case FilesSession: if (active >= 0) { auto &t = *tabs[active]; connect(t.profile, !t.transient, false, false, true); } break;
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
        if (active >= 0 && !tabs[active]->files && tabs[active]->profile.protocol != L"local" && IsWindow(tabs[active]->terminal)) PostMessageW(tabs[active]->terminal, WM_SYSCOMMAND, 0x0050, 0);
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
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, hwnd, nullptr);
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
        int tab = TrackPopupMenu(menu, TPM_RETURNCMD, r.left, r.bottom, 0, hwnd, nullptr);
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
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) PostMessageW(app->hwnd, WM_APP + 47, 0, 0);
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(app->hwnd, WM_COMMAND, GetDlgCtrlID(hwnd) == CommandText ? SendCommand : GetDlgCtrlID(hwnd) == Quick ? QuickConnect : ConnectHost, 0); return 0;
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
            app->controls[Search - Search] = ui::edit(hwnd, L"Name, alias, address…", Search);
            app->controls[Quick - Search] = ui::edit(hwnd, L"user@hostname", Quick);
            SetWindowSubclass(app->control(Search), editProc, 1, (DWORD_PTR)app);
            SetWindowSubclass(app->control(Quick), editProc, 1, (DWORD_PTR)app);
            app->controls[HostList - Search] = ui::control(hwnd, L"LISTBOX", L"Saved hosts", HostList,
                LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP);
            SendMessageW(app->control(HostList), LB_SETITEMHEIGHT, 0, ui::px(65));
            SetWindowSubclass(app->control(HostList), editProc, 1, (DWORD_PTR)app);
            addButton(NavigationToggle, L"Hide hosts");
            addButton(NewHost, L"New host"); addButton(ConnectHost, L"Connect"); addButton(EditHost, L"Edit");
            addButton(FilesHost, L"SFTP"); addButton(FilesSession, L"Files");
            addButton(QuickConnect, L"Connect →"); addButton(HomeNew, L"+ Add a host"); addButton(HomePreview, L"Color preview →");
            addButton(Advanced, L"Host settings"); addButton(Duplicate, L"Duplicate"); addButton(Reconnect, L"Reconnect");
            addButton(SessionSettings, L"Settings"); addButton(Tools, L"Tools"); addButton(About, L"About");
            addButton(LocalCmd, L"Command Prompt"); addButton(LocalPowerShell, L"PowerShell");
            addButton(SaveCurrent, L"Save host…");
            addButton(Split, L"Split");
            app->controls[CommandText-Search] = ui::edit(hwnd, L"Command · Enter to send", CommandText);
            SendMessageW(app->control(CommandText), EM_SETLIMITTEXT, 8192, 0);
            SetWindowSubclass(app->control(CommandText), editProc, 1, (DWORD_PTR)app);
            addButton(SendCommand, L"Send");
            app->controls[SendAll-Search] = ui::checkbox(hwnd, L"Send to targets", SendAll);
            app->controls[SyncInput-Search] = ui::checkbox(hwnd, L"Sync keyboard", SyncInput);
            addButton(Targets, L"Targets");
            app->tooltips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(app->tooltips, TTM_SETMAXTIPWIDTH, 0, ui::px(440));
            for (int id : {NavigationToggle, Search, NewHost, ConnectHost, FilesHost, EditHost, Advanced, SessionSettings}) {
                TOOLINFOW tip{sizeof(tip)}; tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                tip.hwnd = hwnd; tip.uId = (UINT_PTR)app->control(id); tip.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(app->tooltips, TTM_ADDTOOLW, 0, (LPARAM)&tip);
            }
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
        case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize = {ui::px(980), ui::px(740)}; return 0;
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
                ui::label(d->hDC, L">_", ui::rect(13, y + 12, 33, 33), ui::TextSize::body, hostColor(p), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                ui::label(d->hDC, p.displayName(), ui::rect(57, y + 7, sidebarExpanded - 93, 25), ui::TextSize::body, selected ? ui::bright : ui::text, true);
                std::wstring detail = p.group.empty() ? (p.user.empty() ? L"" : p.user + L"@") + p.host : p.group + L" / " + p.host;
                ui::label(d->hDC, detail, ui::rect(57, y + 31, sidebarExpanded - 93, 22), ui::TextSize::caption, ui::muted);
                if (d->itemState & ODS_FOCUS) { RECT f = r; InflateRect(&f,-3,-3); DrawFocusRect(d->hDC,&f); }
            } else ui::drawButton(d, d->CtlID == QuickConnect || d->CtlID == ConnectHost || d->CtlID == NewHost);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == Search && HIWORD(wp) == EN_CHANGE) app->filter();
            else if (LOWORD(wp) == HostList && HIWORD(wp) == LBN_DBLCLK) app->action(ConnectHost);
            else if (LOWORD(wp) == HostList && HIWORD(wp) == LBN_SELCHANGE) app->hostActions();
            else if (HIWORD(wp) == BN_CLICKED) app->action(LOWORD(wp));
            return 0;
        case WM_NOTIFY: {
            auto header = (NMHDR *)lp;
            if (header->hwndFrom == app->tooltips && header->code == TTN_GETDISPINFOW) {
                int id = GetDlgCtrlID((HWND)header->idFrom);
                auto p = app->selectedHost();
                if (id == NavigationToggle) app->tooltipText = L"Show or hide saved connections (Ctrl+Shift+H)";
                else if (id == Search) app->tooltipText = L"Search by name, alias, address, group or username (Ctrl+Shift+P)";
                else if (id == NewHost) app->tooltipText = L"Save a new connection";
                else if (id == SessionSettings) app->tooltipText = L"Change settings for the active terminal";
                else if (!p) app->tooltipText = id == Advanced ? L"Open connection settings" : L"Select a saved connection first";
                else app->tooltipText = std::wstring(id == ConnectHost ? L"Connect to " : id == FilesHost ? L"Open SFTP for " : id == EditHost ? L"Edit " : L"Connection settings for ") +
                    p->displayName() + L"\n" + p->name + L" · " + p->host;
                ((NMTTDISPINFOW *)lp)->lpszText = app->tooltipText.data();
                return 0;
            }
            break;
        }
        case WM_CONTEXTMENU: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (p.x == -1) { RECT r; GetWindowRect(app->control(HostList), &r); p={r.left+30,r.top+30}; }
            if ((HWND)wp == app->control(HostList)) {
                POINT local = p; ScreenToClient(app->control(HostList), &local);
                auto item = SendMessageW(app->control(HostList), LB_ITEMFROMPOINT, 0, MAKELPARAM(local.x, local.y));
                if (!HIWORD(item)) { SendMessageW(app->control(HostList), LB_SETCURSEL, LOWORD(item), 0); app->hostActions(); }
                app->hostMenu(p);
            } else {
                POINT local=p; ScreenToClient(hwnd,&local);
                for (auto hit: app->hits) if (!hit.close && PtInRect(&hit.rect,local)) { app->tabMenu(hit.index,p); break; }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (app->active >= 0 && app->panes.size() > 1) {
                for (size_t i=0;i<app->panes.size();++i) {
                    auto r=app->paneRects[i]; RECT frame=ui::rect(r.x,r.y,r.width,r.height);
                    RECT zoom=ui::rect(r.x+r.width-62,r.y+8,53,28);
                    if (r.width > 0 && PtInRect(&frame,p)) for (size_t j=0;j<app->tabs.size();++j) if (app->tabs[j].get()==app->panes[i]) {
                        app->select((int)j); if (PtInRect(&zoom,p)) app->zoomPane(); return 0;
                    }
                }
            }
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
                if ((int)i==app->active) app->focusActive();
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
        case WM_APP + 46: {
            // Notifications can be queued across rapid clicks or arrive after
            // focus has moved to the command field. Read the actual owner.
            for (const auto &tab : app->tabs) if (tab->terminal == (HWND)wp) {
                app->refreshPaneFocus(); break;
            }
            return 0;
        }
        case WM_APP + 47: app->refreshPaneFocus(); return 0;
        case WM_ACTIVATE: PostMessageW(hwnd, WM_APP + 47, 0, 0); break;
        case WM_COPYDATA: {
            bool owned = false;
            for (auto &t : app->tabs) if (t->terminal == (HWND)wp) owned = true;
            auto *data = (const COPYDATASTRUCT *)lp;
            if (owned && data && data->dwData == WSHELL_INPUT_MESSAGE) { app->relayInput((HWND)wp, data); return TRUE; }
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
            app->focusActive();
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
    try {
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        auto command = std::wstring(commandLine);
        bool terminal = command == L"--terminal" || command.starts_with(L"--terminal ");
#ifdef WOOK_UI_TEST
        bool fontProbe = command == L"--font-probe";
        if (!terminal && !fontProbe) {
            auto isolated = wook::executableDirectory() + L"\\ui-data-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
            SetEnvironmentVariableW(L"WOOK_DATA_DIR", isolated.c_str());
            fontPresentBeforeLoad = testFontEnumerable();
        }
#endif
        wshellLoadFonts();
#ifdef WOOK_UI_TEST
        if (fontProbe) return testFontEnumerable() ? 0 : 2;
#endif
        if (terminal) {
            auto arguments = wook::utf8(command);
            return wshellTerminalMain(instance, nullptr, arguments.data(), show);
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        App app; app.directory=wook::executableDirectory();
        app.navigationVisible = wook::loadNavigationVisible();
        app.sidebar = app.navigationVisible ? sidebarExpanded : 0;
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
                else if (ctrl && shift && msg.wParam=='S') command=11;
                else if (ctrl && shift && msg.wParam==VK_RETURN) command=12;
                else if (ctrl && shift && msg.wParam=='B') command=13;
                else if (ctrl && shift && msg.wParam=='K') command=14;
                else if (ctrl && shift && msg.wParam=='H') command=15;
                else if (ctrl && alt && msg.wParam>=VK_LEFT && msg.wParam<=VK_DOWN) command=30+(int)(msg.wParam-VK_LEFT);
                else if (alt && msg.wParam>='1' && msg.wParam<='9') command=20+(int)(msg.wParam-'1');
                else if (msg.wParam==VK_F11) command=8;
                if (command) {
                    try { app.command(command); } catch (const std::exception &e) { ui::error(hwnd,e); }
                    consumed=true;
                }
            }
            if (!consumed && !IsDialogMessageW(hwnd,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        for (auto &tab:app.tabs) if (tab->process) WaitForSingleObject(tab->process,200);
        CloseHandle(app.job);
        for (auto &tab:app.tabs) if (tab->transient) {
            wchar_t *path=wsPath(L"sessions",wook::utf8(tab->storageName).c_str()); wsRemove(path); free(path);
        }
        for (auto &[key,font]:ui::fonts) DeleteObject(font);
        return 0;
    } catch (const std::exception &e) { ui::error(nullptr,e); return 1; }
}
