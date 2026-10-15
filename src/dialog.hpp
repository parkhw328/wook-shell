#pragma once
#include "core.hpp"
bool editHost(HWND owner, wook::Profile &profile, bool existing);
bool editFolder(HWND owner, std::wstring &name, bool existing);
void showAbout(HWND owner);
