#pragma once
#include "keys.h"
#include "key_import.h"
#include <string>
#include <vector>

std::wstring preparePrivateKey(HWND owner, const std::wstring &path);
std::wstring registerPrivateKey(WsKey *key, const std::wstring &name, const char *passphrase);
std::vector<std::wstring> registeredPrivateKeys();
std::wstring registeredKeyName(const std::wstring &path);
