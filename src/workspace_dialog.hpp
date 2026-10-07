#pragma once
#include <windows.h>
#include <cstddef>

bool confirmTabClose(HWND owner, size_t tabs, size_t live);
void showApplicationSettings(HWND owner);
