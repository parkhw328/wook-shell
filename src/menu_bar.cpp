#include "menu_bar.hpp"
#include "core.hpp"
#include "ui.hpp"
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace ui {
namespace {
const UINT paintPopupBorder = RegisterWindowMessageW(L"wShell.PaintMenuBorder");
LRESULT CALLBACK popupBorderProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR menu) {
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, popupBorderProc, id);
    auto result = message == paintPopupBorder ? 0 : DefSubclassProc(window, message, wp, lp);
    if (message == WM_PAINT || message == WM_NCPAINT || message == paintPopupBorder) {
        MENUBARINFO info{sizeof(info)};
        if (GetMenuBarInfo(window, OBJID_CLIENT, 0, &info) && info.hMenu == (HMENU)menu) {
            RECT rect{}; GetWindowRect(window, &rect); OffsetRect(&rect, -rect.left, -rect.top);
            auto dc = GetWindowDC(window);
            if (dc) {
                SetDCBrushColor(dc, line); FrameRect(dc, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
                ReleaseDC(window, dc);
            }
        }
    }
    return result;
}
}
void MenuBar::themePopup() const {
    // A popup has its own HWND and native frame. Discover it only on this UI
    // thread and match its HMENU; unrelated dialogs/context menus are untouched.
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        wchar_t kind[32]{}; GetClassNameW(window, kind, 32);
        if (wcscmp(kind, L"#32768")) return TRUE;
        auto &bar = *(const MenuBar *)data;
        MENUBARINFO info{sizeof(info)};
        if (!GetMenuBarInfo(window, OBJID_CLIENT, 0, &info) || !bar.contains(info.hMenu)) return TRUE;
        DWORD_PTR previous = 0;
        if (!GetWindowSubclass(window, popupBorderProc, 1, &previous) || previous != (DWORD_PTR)info.hMenu) {
            if (SetWindowSubclass(window, popupBorderProc, 1, (DWORD_PTR)info.hMenu))
                PostMessageW(window, paintPopupBorder, 0, 0);
        }
        return TRUE;
    }, (LPARAM)this);
}
void MenuBar::append(HMENU parent, unsigned id, const wchar_t *text, HMENU popup) {
    items.emplace_back();
    auto &entry = items.back();
    entry.text = text ? text : L""; entry.parent = parent; entry.id = id;
    entry.heading = popup != nullptr; entry.separator = text == nullptr;
    for (size_t i = 0; i < entry.text.size(); ++i) {
        if (entry.text[i] == L'&' && i + 1 < entry.text.size()) ++i;
        entry.name += entry.text[i];
    }
    entry.accessible = {MSAA_MENU_SIG, (DWORD)entry.name.size(), entry.name.data()};
    MENUITEMINFOW info{sizeof(info)};
    info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID;
    info.fType = MFT_OWNERDRAW | (entry.separator ? MFT_SEPARATOR : 0);
    info.wID = id; info.dwItemData = (ULONG_PTR)&entry;
    if (!entry.separator) { info.fMask |= MIIM_STRING; info.dwTypeData = entry.text.data(); }
    if (popup) { info.fMask |= MIIM_SUBMENU; info.hSubMenu = popup; }
    if (!InsertMenuItemW(parent, GetMenuItemCount(parent), TRUE, &info))
        throw std::runtime_error("Cannot create a menu item.");
}
void MenuBar::create(HWND window, std::initializer_list<MenuSection> sections) {
    destroy(); owner = window; menu = CreateMenu(); background = CreateSolidBrush(panel);
    if (!menu || !background) throw std::runtime_error("Cannot create the application menu.");
    MENUINFO info{sizeof(info)}; info.fMask = MIM_BACKGROUND | MIM_STYLE;
    info.hbrBack = background; info.dwStyle = MNS_NOCHECK;
    SetMenuInfo(menu, &info);
    for (const auto &section : sections) {
        auto popup = CreatePopupMenu();
        if (!popup) throw std::runtime_error("Cannot create an application submenu.");
        try { append(menu, 0, section.text, popup); }
        catch (...) { DestroyMenu(popup); throw; }
        SetMenuInfo(popup, &info);
        for (const auto &command : section.commands) append(popup, command.id, command.text);
    }
    if (!SetMenu(owner, menu)) throw std::runtime_error("Cannot attach the application menu.");
    DrawMenuBar(owner);
}
void MenuBar::destroy() {
    if (menu) {
        if (IsWindow(owner) && GetMenu(owner) == menu) SetMenu(owner, nullptr);
        DestroyMenu(menu); menu = nullptr;
    }
    items.clear();
    if (background) { DeleteObject(background); background = nullptr; }
    owner = nullptr;
}
bool MenuBar::contains(HMENU popup) const {
    if (!menu || !popup) return false;
    if (popup == menu) return true;
    for (int i = 0; i < GetMenuItemCount(menu); ++i) if (GetSubMenu(menu, i) == popup) return true;
    return false;
}
bool MenuBar::enabled(unsigned id) const {
    auto state = GetMenuState(menu, id, MF_BYCOMMAND);
    return state != (UINT)-1 && !(state & (MF_DISABLED | MF_GRAYED));
}
void MenuBar::enable(unsigned id, bool value) {
    if (menu) EnableMenuItem(menu, id, MF_BYCOMMAND | (value ? MF_ENABLED : MF_GRAYED));
}
void MenuBar::check(unsigned id, bool value) {
    if (menu) CheckMenuItem(menu, id, MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
}
bool MenuBar::measure(MEASUREITEMSTRUCT &item) const {
    if (item.CtlType != ODT_MENU) return false;
    for (const auto &entry : items) if (item.itemData == (ULONG_PTR)&entry) {
        if (entry.separator) { item.itemWidth = 0; item.itemHeight = px(13); return true; }
        // Windows fixes owner-drawn menu bar height to its menu metric. Use the
        // shared caption font there so the glyphs retain vertical breathing room.
        auto dc = GetDC(owner); auto old = SelectObject(dc, font(entry.heading ? TextSize::caption : TextSize::body));
        auto tab = entry.text.find(L'\t');
        RECT label{}; DrawTextW(dc, entry.text.c_str(), tab == std::wstring::npos ? -1 : (int)tab, &label, DT_CALCRECT | DT_SINGLELINE);
        RECT shortcut{};
        if (tab != std::wstring::npos) DrawTextW(dc, entry.text.c_str() + tab + 1, -1, &shortcut, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old); ReleaseDC(owner, dc);
        item.itemWidth = label.right + (entry.heading ? px(36) : px(64)) +
            (shortcut.right ? shortcut.right + px(28) : 0);
        item.itemHeight = entry.heading ? GetSystemMetricsForDpi(SM_CYMENUSIZE, dpi) :
            std::max(px(38), (int)label.bottom + px(16));
        return true;
    }
    return false;
}
bool MenuBar::draw(const DRAWITEMSTRUCT &item) const {
    if (item.CtlType != ODT_MENU) return false;
    for (const auto &entry : items) if (item.itemData == (ULONG_PTR)&entry) {
        if (!entry.heading) themePopup();
        bool selected = item.itemState & (ODS_SELECTED | ODS_HOTLIGHT);
        bool disabled = item.itemState & (ODS_DISABLED | ODS_GRAYED);
        if (entry.separator) {
            fill(item.hDC, item.rcItem, panel);
            auto rect = item.rcItem;
            rect.left += px(16); rect.right -= px(16);
            rect.top += (rect.bottom - rect.top) / 2; rect.bottom = rect.top + 1;
            fill(item.hDC, rect, line);
            return true;
        }
        fill(item.hDC, item.rcItem, selected ? raised : panel);
        if (selected && entry.heading) {
            auto lineRect = item.rcItem; lineRect.top = lineRect.bottom - px(2);
            fill(item.hDC, lineRect, accent);
        }
        auto rect = item.rcItem;
        rect.left += px(entry.heading ? 18 : 32); rect.right -= px(entry.heading ? 18 : 20);
        auto tab = entry.text.find(L'\t'); auto label = entry.text.substr(0, tab);
        auto old = SelectObject(item.hDC, font(entry.heading ? TextSize::caption : TextSize::body));
        SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, disabled ? muted : selected ? bright : text);
        UINT flags = DT_SINGLELINE | DT_VCENTER | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
        DrawTextW(item.hDC, label.c_str(), -1, &rect, flags);
        if (tab != std::wstring::npos) {
            auto shortcut = entry.text.substr(tab + 1);
            DrawTextW(item.hDC, shortcut.c_str(), -1, &rect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        if (!entry.heading && (item.itemState & ODS_CHECKED)) {
            auto checkRect = item.rcItem; checkRect.right = checkRect.left + px(28);
            ui::label(item.hDC, L"✓", checkRect, TextSize::body, disabled ? muted : accent, true,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(item.hDC, old);
        return true;
    }
    return false;
}
void MenuBar::drawBorder() const {
    if (!menu || !IsWindow(owner) || IsIconic(owner) || GetMenu(owner) != menu) return;
    RECT window{}, client{};
    if (!GetWindowRect(owner, &window) || !GetClientRect(owner, &client)) return;
    MapWindowPoints(owner, nullptr, (POINT *)&client, 2);
    auto dc = GetWindowDC(owner);
    if (!dc) return;
    MENUBARINFO bar{sizeof(bar)};
    if (GetMenuBarInfo(owner, OBJID_MENU, 0, &bar)) {
        auto unused = bar.rcBar;
        for (int i = 0; i < GetMenuItemCount(menu); ++i) {
            RECT item{};
            if (GetMenuItemRect(owner, menu, i, &item)) unused.left = std::max(unused.left, item.right);
        }
        OffsetRect(&unused, -window.left, -window.top);
        fill(dc, unused, panel);
        // The native bar also reserves a one-pixel top edge outside its items.
        auto top = bar.rcBar; top.bottom = top.top + 1;
        OffsetRect(&top, -window.left, -window.top); fill(dc, top, panel);
        --top.top; --top.bottom; fill(dc, top, line);
    }
    // Replace the light non-client edge below HMENU, leaving the caption,
    // resize frame and native hit testing intact.
    RECT edge{client.left - window.left, client.top - window.top - 1,
        client.right - window.left, client.top - window.top};
    fill(dc, edge, line); ReleaseDC(owner, dc);
}
LRESULT MenuBar::mnemonic(HMENU popup, wchar_t key) const {
    std::vector<int> matches;
    int highlighted = -1;
    for (int i = 0; i < GetMenuItemCount(popup); ++i) {
        MENUITEMINFOW info{sizeof(info)}; info.fMask = MIIM_DATA | MIIM_STATE;
        if (!GetMenuItemInfoW(popup, i, TRUE, &info)) continue;
        if (info.fState & MFS_HILITE) highlighted = i;
        if (info.fState & (MFS_DISABLED | MFS_GRAYED)) continue;
        for (const auto &entry : items) if (info.dwItemData == (ULONG_PTR)&entry) {
            auto marker = entry.text.find(L'&');
            if (marker != std::wstring::npos && marker + 1 < entry.text.size() &&
                towupper(entry.text[marker + 1]) == towupper(key)) matches.push_back(i);
        }
    }
    if (matches.empty()) return MAKELRESULT(0, MNC_IGNORE);
    auto next = std::find_if(matches.begin(), matches.end(), [&](int index) { return index > highlighted; });
    return MAKELRESULT(next == matches.end() ? matches.front() : *next, matches.size() == 1 ? MNC_EXECUTE : MNC_SELECT);
}
}
