#pragma once
#include "keys.h"
#include "key_import.h"
#include <string>
#include <vector>

// nullptr prompts when needed; a supplied passphrase (including "") never prompts.
// Public input resolves to a matching private key before registration or use.
std::wstring preparePrivateKey(HWND owner, const std::wstring &path, const char *passphrase = nullptr);
std::wstring registerPrivateKey(WsKey *key, const std::wstring &name, const char *passphrase);
std::vector<std::wstring> registeredPrivateKeys();
std::wstring registeredKeyName(const std::wstring &path);
