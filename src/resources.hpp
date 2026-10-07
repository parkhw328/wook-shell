#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <string>

namespace wook {
std::string resourceBytes(int id);
std::unique_ptr<Gdiplus::Bitmap> loadWordmark();
void showLicenses(HWND owner);
}
extern "C" void wshellLoadFonts(void);
