#pragma once
#include <windows.h>
#include <string>

namespace wook {
std::string resourceBytes(int id);
void showLicenses(HWND owner);
}
extern "C" void wshellLoadFonts(void);
