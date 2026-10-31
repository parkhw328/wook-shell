#pragma once
#include <windows.h>
#include <oleacc.h>
#include <initializer_list>
#include <list>
#include <string>

namespace ui {
struct MenuCommand { unsigned id; const wchar_t *text; };
struct MenuSection { const wchar_t *text; std::initializer_list<MenuCommand> commands; };

// Native HMENU owns input routing. Owner drawing supplies the shared font and
// palette; stable item data also exposes the menu names through MSAA.
class MenuBar {
    struct Item {
        MSAAMENUINFO accessible{}; // Must be first for Windows accessibility.
        std::wstring text, name;
        HMENU parent = nullptr;
        unsigned id = 0;
        bool heading = false;
    };
    HWND owner = nullptr;
    HMENU menu = nullptr;
    HBRUSH background = nullptr;
    std::list<Item> items;
    void append(HMENU parent, unsigned id, const wchar_t *text, HMENU popup = nullptr);
public:
    MenuBar() = default;
    MenuBar(const MenuBar &) = delete;
    MenuBar &operator=(const MenuBar &) = delete;
    ~MenuBar() { destroy(); }
    void create(HWND window, std::initializer_list<MenuSection> sections);
    void destroy();
    HMENU handle() const { return menu; }
    bool contains(HMENU popup) const;
    bool enabled(unsigned id) const;
    void enable(unsigned id, bool value);
    void check(unsigned id, bool value);
    bool measure(MEASUREITEMSTRUCT &item) const;
    bool draw(const DRAWITEMSTRUCT &item) const;
    LRESULT mnemonic(HMENU popup, wchar_t key) const;
};
}
