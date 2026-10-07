#include "core.hpp"
#include "ui.hpp"
#include "resources.hpp"
#include <stdexcept>

namespace wook {
std::string resourceBytes(int id) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    auto size = resource ? SizeofResource(module, resource) : 0;
    auto memory = resource ? LoadResource(module, resource) : nullptr;
    auto bytes = memory ? LockResource(memory) : nullptr;
    if (!bytes || !size) throw std::runtime_error("An embedded wShell resource is missing.");
    return std::string((const char *)bytes, size);
}
std::unique_ptr<Gdiplus::Bitmap> loadWordmark() {
    auto bytes = resourceBytes(102);
    auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) throw std::bad_alloc();
    void *target = GlobalLock(memory);
    if (!target) { GlobalFree(memory); throw std::bad_alloc(); }
    memcpy(target, bytes.data(), bytes.size()); GlobalUnlock(memory);
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) { GlobalFree(memory); throw std::runtime_error("Cannot load branding image."); }
    auto image = std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromStream(stream));
    std::unique_ptr<Gdiplus::Bitmap> copy;
    if (image && image->GetLastStatus() == Gdiplus::Ok)
        copy.reset(image->Clone(0, 0, image->GetWidth(), image->GetHeight(), PixelFormat32bppARGB));
    image.reset(); stream->Release();
    if (!copy) throw std::runtime_error("Embedded branding image is invalid.");
    return copy;
}
namespace {
LRESULT CALLBACK licenseProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        ui::dark(hwnd);
        auto edit = ui::control(hwnd, L"EDIT", L"", 1, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
        SendMessageW(edit, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
        auto text = wide(resourceBytes(105));
        std::wstring lines;
        for (auto c : text) { if (c == L'\r') continue; if (c == L'\n') lines += L'\r'; lines += c; }
        SetWindowTextW(edit, lines.c_str()); return 0;
    }
    if (msg == WM_SIZE) { MoveWindow(GetDlgItem(hwnd, 1), 0, 0, LOWORD(lp), HIWORD(lp), TRUE); return 0; }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)wp, ui::text); SetBkColor((HDC)wp, ui::panel);
        static HBRUSH brush = CreateSolidBrush(ui::panel); return (LRESULT)brush;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
void showLicenses(HWND owner) {
    WNDCLASSW wc{}; wc.lpfnWndProc = licenseProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wShellLicenses"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    auto window = CreateWindowExW(0, wc.lpszClassName, L"wShell · Open-source licenses", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, ui::px(850), ui::px(620), owner, nullptr, wc.hInstance, nullptr);
    ShowWindow(window, SW_SHOWNORMAL);
}
}
extern "C" void wshellLoadFonts(void) {
    static bool loaded = false;
    if (loaded) return;
    for (int id : {103, 104}) {
        auto module = GetModuleHandleW(nullptr);
        auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
        DWORD count = 0;
        if (resource) AddFontMemResourceEx(LockResource(LoadResource(module, resource)), SizeofResource(module, resource), nullptr, &count);
    }
    loaded = true;
}
