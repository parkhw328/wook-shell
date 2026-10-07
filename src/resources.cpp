#include "core.hpp"
#include "ui.hpp"
#include "resources.hpp"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
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
namespace {
std::filesystem::path cacheFont(int id, const wchar_t *weight) {
    auto bytes = resourceBytes(id);
    unsigned char digest[32];
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)bytes.data(),
                   (ULONG)bytes.size(), digest, sizeof(digest)) < 0)
        throw std::runtime_error("Cannot verify the embedded font.");
    std::wstring hash;
    for (auto byte : digest) { hash += L"0123456789abcdef"[byte >> 4]; hash += L"0123456789abcdef"[byte & 15]; }
    auto root = wsRoot();
    if (!root) throw std::runtime_error("Cannot locate the font cache folder.");
    auto folder = std::filesystem::path(root) / L"fonts"; free(root);
    std::filesystem::create_directories(folder);
    auto path = folder / (std::wstring(L"JetBrainsMono-") + weight + L"-" + hash + L".ttf");
    auto matches = [&] {
        std::error_code error;
        if (std::filesystem::file_size(path, error) != bytes.size() || error) return false;
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {}) == bytes;
    };
    if (!matches()) {
        // Publish a complete file atomically. Concurrent tabs can reuse the
        // same content-addressed cache without replacing a font in use.
        auto temporary = path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write the bundled font cache. Check folder permissions.");
        DWORD written = 0;
        bool ok = WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) || matches();
        DeleteFileW(temporary.c_str());
        if (!ok) throw std::runtime_error("Cannot save the bundled font cache. Check disk space and folder permissions.");
    }
    return path;
}
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
        auto path = wook::cacheFont(id, id == 103 ? L"Regular" : L"Bold");
        // Memory fonts are not enumerable by ChooseFont. FR_PRIVATE keeps
        // this font local to wShell while allowing native font selection.
        if (!AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr))
            throw std::runtime_error("Windows could not load the bundled JetBrains Mono font.");
    }
    loaded = true;
}
