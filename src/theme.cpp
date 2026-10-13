#include "core.hpp"
#include "ui.hpp"
#include "theme.h"

extern "C" void wsThemeScrollbars(HWND window) {
    // Classic lists, multiline edits and custom windows need the ScrollBar
    // class explicitly. The association also covers bars that appear later.
    if (window) SetWindowTheme(window, L"DarkMode_Explorer", L"ScrollBar");
}

extern "C" void wsThemeControl(HWND control) {
    if (!control) return;
    wchar_t kind[64]{}; GetClassNameW(control, kind, 64);
    auto style = GetWindowLongPtrW(control, GWL_STYLE);
    if (!_wcsicmp(kind, WC_LISTVIEWW) || !_wcsicmp(kind, WC_TREEVIEWW)) {
        // Common controls keep their own theme parts, including headers.
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    } else if (!_wcsicmp(kind, L"ComboBox")) {
        SetWindowTheme(control, L"DarkMode_CFD", nullptr);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(control, &info)) {
            wsThemeScrollbars(info.hwndList);
            if (info.hwndItem) SetWindowTheme(info.hwndItem, L"DarkMode_CFD", nullptr);
        }
    } else if (!_wcsicmp(kind, L"ListBox") || !_wcsicmp(kind, L"ComboLBox") ||
               !_wcsicmp(kind, L"ScrollBar") || (style & (WS_VSCROLL | WS_HSCROLL)) ||
               (!_wcsicmp(kind, L"Edit") && (style & ES_MULTILINE))) {
        wsThemeScrollbars(control);
    } else {
        SetWindowTheme(control, L"DarkMode_CFD", nullptr);
    }
}

namespace {
LRESULT CALLBACK eventLogThemeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, eventLogThemeProc, id);
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORLISTBOX ||
        msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORBTN) {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::panel);
        SetDCBrushColor((HDC)wp, ui::panel); return (LRESULT)GetStockObject(DC_BRUSH);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
}
extern "C" void wsThemeEventLog(HWND dialog) {
    ui::dark(dialog);
    SetWindowSubclass(dialog, eventLogThemeProc, 1, 0);
    EnumChildWindows(dialog, [](HWND control, LPARAM) -> BOOL { wsThemeControl(control); return TRUE; }, 0);
}
