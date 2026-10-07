#pragma once
#include <windows.h>
#include <string>
HWND createSftpView(HWND owner, HANDLE job, const std::wstring &session, bool saved);
