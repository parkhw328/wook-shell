#pragma once
#include <windows.h>
#include <string>
HWND createSftpView(HWND owner, HANDLE job, const std::wstring &session, bool saved, const std::wstring &passwordSession = {}, const std::wstring &remotePath = {});
