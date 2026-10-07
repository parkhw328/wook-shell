#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>

inline std::wstring testPassword() {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"WOOK_TEST_PASSWORD_FILE", path, 32768)) throw std::runtime_error("Missing password fixture");
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    std::string text; std::getline(input, text);
    if (text.empty()) throw std::runtime_error("Empty password fixture");
    return wook::wide(text);
}

inline void captureTestWindow(HWND window, const wchar_t *name) {
    if (IsIconic(window)) { ShowWindow(window, SW_RESTORE); UpdateWindow(window); }
    RECT r{}; GetClientRect(window, &r);
    HDC source = GetDC(window), target = CreateCompatibleDC(source);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.right; info.bmiHeader.biHeight = -r.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    auto bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DWORD error = GetLastError(); DeleteDC(target); ReleaseDC(window, source);
        throw std::runtime_error("Cannot capture the test window (" + std::to_string(r.right) + "x" + std::to_string(r.bottom) + ", Windows error " + std::to_string(error) + ").");
    }
    auto old = SelectObject(target, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_INVALIDATE);
    BitBlt(target, 0,0,r.right,r.bottom,source,0,0,SRCCOPY);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + r.right * r.bottom * 4;
    auto path = std::filesystem::path(wook::executableDirectory()) / name;
    std::ofstream out(path, std::ios::binary);
    out.write((char *)&header, sizeof(header));
    out.write((char *)&info.bmiHeader, sizeof(BITMAPINFOHEADER));
    out.write((char *)pixels, r.right * r.bottom * 4);
    SelectObject(target, old); DeleteObject(bitmap); DeleteDC(target); ReleaseDC(window, source);
    if (!out) throw std::runtime_error("Cannot write screenshot artifact.");
}
