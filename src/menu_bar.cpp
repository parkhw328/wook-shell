#include "menu_bar.hpp"
#include "core.hpp"
#include "ui.hpp"
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace ui {
void MenuBar::append(HMENU parent, unsigned id, const wchar_t *text, HMENU popup) {
    if (!text) {
        if (!AppendMenuW(parent, MF_SEPARATOR, 0, nullptr)) throw std::runtime_error("Cannot create a menu separator.");
        return;
    }
    items.emplace_back();
    auto &entry = items.back();
    entry.text = text; entry.parent = parent; entry.id = id; entry.heading = popup != nullptr;
    for (size_t i = 0; i < entry.text.size(); ++i) {
        if (entry.text[i] == L'&' && i + 1 < entry.text.size()) ++i;
        entry.name += entry.text[i];
    }
    entry.accessible = {MSAA_MENU_SIG, (DWORD)entry.name.size(), entry.name.data()};
    MENUITEMINFOW info{sizeof(info)};
    info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID | MIIM_STRING;
    info.fType = MFT_OWNERDRAW;
    info.wID = id; info.dwItemData = (ULONG_PTR)&entry;
    info.dwTypeData = entry.text.data();
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
        auto dc = GetDC(owner); auto old = SelectObject(dc, font());
        auto tab = entry.text.find(L'\t');
        RECT label{}; DrawTextW(dc, entry.text.c_str(), tab == std::wstring::npos ? -1 : (int)tab, &label, DT_CALCRECT | DT_SINGLELINE);
        RECT shortcut{};
        if (tab != std::wstring::npos) DrawTextW(dc, entry.text.c_str() + tab + 1, -1, &shortcut, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old); ReleaseDC(owner, dc);
        item.itemWidth = label.right + (entry.heading ? px(20) : px(64)) +
            (shortcut.right ? shortcut.right + px(28) : 0);
        item.itemHeight = std::max(px(entry.heading ? 30 : 32), (int)label.bottom + px(10));
        return true;
    }
    return false;
}
bool MenuBar::draw(const DRAWITEMSTRUCT &item) const {
    if (item.CtlType != ODT_MENU) return false;
    for (const auto &entry : items) if (item.itemData == (ULONG_PTR)&entry) {
        bool selected = item.itemState & (ODS_SELECTED | ODS_HOTLIGHT);
        bool disabled = item.itemState & (ODS_DISABLED | ODS_GRAYED);
        fill(item.hDC, item.rcItem, selected ? raised : panel);
        if (selected && entry.heading) {
            auto lineRect = item.rcItem; lineRect.top = lineRect.bottom - px(2);
            fill(item.hDC, lineRect, accent);
        }
        auto rect = item.rcItem;
        rect.left += px(entry.heading ? 10 : 32); rect.right -= px(entry.heading ? 10 : 20);
        auto tab = entry.text.find(L'\t'); auto label = entry.text.substr(0, tab);
        auto old = SelectObject(item.hDC, font());
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
