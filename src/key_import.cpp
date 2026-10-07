#include "core.hpp"
#include "ui.hpp"
#include "key_import.hpp"
#include "prompt.h"
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
std::filesystem::path keyDirectory() {
    auto root = wsRoot();
    if (!root) throw std::runtime_error("Cannot locate the private key library.");
    auto directory = std::filesystem::path(root) / L"keys"; free(root);
    return directory;
}
}
std::wstring registeredKeyName(const std::wstring &path) {
    auto name = std::filesystem::path(path).stem().wstring();
    // Imported keys have a readable name plus a UUID, preventing replacement
    // of another key (or a differently encrypted copy) with the same name.
    auto suffix = name.rfind(L"--");
    if (suffix != std::wstring::npos && name.size() - suffix == 38) name.resize(suffix);
    return name;
}
std::vector<std::wstring> registeredPrivateKeys() {
    std::vector<std::wstring> keys;
    auto directory = keyDirectory();
    if (!std::filesystem::exists(directory)) return keys;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && _wcsicmp(entry.path().extension().c_str(), L".ppk") == 0) keys.push_back(entry.path().wstring());
    std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return keys;
}
std::wstring registerPrivateKey(WsKey *key, const std::wstring &name, const char *passphrase) {
    auto directory = keyDirectory(); std::filesystem::create_directories(directory);
    auto stem = std::filesystem::path(name).stem().wstring();
    for (auto &c : stem) if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos) c = L'_';
    if (stem.empty()) stem = L"SSH key";
    if (stem.size() > 60) {
        stem.resize(60);
        if (stem.back() >= 0xD800 && stem.back() <= 0xDBFF) stem.pop_back();
    }
    GUID id{}; wchar_t suffix[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, suffix, 40)) throw std::runtime_error("Cannot allocate a private key entry.");
    auto path = directory / (stem + L"--" + std::wstring(suffix + 1, 36) + L".ppk");
    // The existing writer creates a protected user+SYSTEM ACL, writes to an
    // equally protected temporary file and atomically publishes the result.
    if (!wsKeySave(key, path.c_str(), passphrase)) throw std::runtime_error(wsKeyError());
    return path.wstring();
}
std::wstring preparePrivateKey(HWND owner, const std::wstring &path) {
    if (path.empty()) return path;
    int encrypted = 0, kind = wsKeyInspect(path.c_str(), &encrypted);
    if (kind == WS_KEY_INVALID || kind == WS_KEY_PUBLIC) throw std::runtime_error(wsKeyError());
    if (kind == WS_KEY_PPK) return std::filesystem::absolute(path).wstring();
    struct Secret { char text[4096]{}; ~Secret() { SecureZeroMemory(text, sizeof(text)); } } passphrase;
    if (encrypted && !wsPrompt(owner, "Import private key", "Enter this private key's passphrase. The registered key keeps the same passphrase; it is not saved separately.",
                               1, passphrase.text, sizeof(passphrase.text))) return L"";
    std::unique_ptr<WsKey, decltype(&wsKeyFree)> key(wsKeyLoad(path.c_str(), passphrase.text), wsKeyFree);
    if (!key) throw std::runtime_error(wsKeyError());
    return registerPrivateKey(key.get(), path, passphrase.text);
}
extern "C" wchar_t *wsPreparePrivateKey(HWND owner, const wchar_t *path) {
    try {
        auto result = preparePrivateKey(owner, path ? path : L"");
        if (result.empty()) return nullptr;
        auto copy = (wchar_t *)malloc((result.size() + 1) * sizeof(wchar_t));
        if (!copy) throw std::runtime_error("Cannot allocate the private key path.");
        memcpy(copy, result.c_str(), (result.size() + 1) * sizeof(wchar_t)); return copy;
    } catch (const std::exception &error) { ui::error(owner, error); return nullptr; }
}
