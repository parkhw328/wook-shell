#include "core.hpp"
#include "ui.hpp"
#include "key_import.hpp"
#include "prompt.h"
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
using Key = std::unique_ptr<WsKey, decltype(&wsKeyFree)>;
struct Secret {
    char text[4096]{};
    ~Secret() { SecureZeroMemory(text, sizeof(text)); }
};
bool readPassphrase(HWND owner, const std::wstring &path, const char *supplied, Secret &secret) {
    if (supplied) {
        if (strlen(supplied) >= sizeof(secret.text)) throw std::runtime_error("The key passphrase is too long.");
        memcpy(secret.text, supplied, strlen(supplied) + 1); return true;
    }
    auto label = "Enter the passphrase for " + wook::utf8(std::filesystem::path(path).filename().wstring()) +
        ". The registered key keeps the same passphrase; it is not saved separately.";
    return wsPrompt(owner, "Import private key", label.c_str(), 1, secret.text, sizeof(secret.text)) != 0;
}
std::filesystem::path keyDirectory() {
    auto root = wsRoot();
    if (!root) throw std::runtime_error("Cannot locate the private key library.");
    auto directory = std::filesystem::path(root) / L"keys"; free(root);
    return directory;
}
bool correspondingName(const std::filesystem::path &publicPath, const std::filesystem::path &candidate) {
    auto name = publicPath.stem().wstring(), file = candidate.filename().wstring();
    if (!_wcsicmp(name.c_str(), file.c_str())) return true; // id_ed25519.pub -> id_ed25519
    for (auto extension : {L".ppk", L".pem", L".key", L".openssh"})
        if (!_wcsicmp((name + extension).c_str(), file.c_str())) return true;
    return false;
}
std::wstring resolvePublicKey(HWND owner, const std::wstring &path, const char *supplied) {
    std::unique_ptr<WsPublicKey, decltype(&wsPublicKeyFree)> identity(wsPublicKeyLoad(path.c_str()), wsPublicKeyFree);
    if (!identity) throw std::runtime_error(wsKeyError());
    // Registered PPKs expose their public identity even when encrypted. Reuse
    // them before decrypting/importing any source key or asking for a password.
    for (const auto &candidate : registeredPrivateKeys())
        if (wsKeyInspect(candidate.c_str(), nullptr) == WS_KEY_PPK && wsPublicKeyMatchesFile(identity.get(), candidate.c_str()))
            return std::filesystem::absolute(candidate).wstring();

    auto publicPath = std::filesystem::absolute(path);
    std::vector<std::wstring> candidates, encryptedCandidates;
    for (const auto &entry : std::filesystem::directory_iterator(publicPath.parent_path()))
        if (entry.is_regular_file() && _wcsicmp(entry.path().c_str(), publicPath.c_str())) candidates.push_back(entry.path().wstring());
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    // Check PPKs first so an existing matching file never creates a new entry.
    for (const auto &candidate : candidates)
        if (wsKeyInspect(candidate.c_str(), nullptr) == WS_KEY_PPK && wsPublicKeyMatchesFile(identity.get(), candidate.c_str())) return candidate;
    for (const auto &candidate : candidates) {
        int encrypted = 0;
        if (wsKeyInspect(candidate.c_str(), &encrypted) != WS_KEY_IMPORT) continue;
        if (encrypted) {
            if (correspondingName(publicPath, candidate)) encryptedCandidates.push_back(candidate);
            continue;
        }
        Key key(wsKeyLoad(candidate.c_str(), ""), wsKeyFree);
        if (key && wsPublicKeyMatchesKey(identity.get(), key.get())) return registerPrivateKey(key.get(), candidate, "");
    }
    for (const auto &candidate : encryptedCandidates) {
        Secret passphrase;
        if (!readPassphrase(owner, candidate, supplied, passphrase)) return L"";
        Key key(wsKeyLoad(candidate.c_str(), passphrase.text), wsKeyFree);
        if (!key) throw std::runtime_error(wsKeyError());
        if (wsPublicKeyMatchesKey(identity.get(), key.get())) return registerPrivateKey(key.get(), candidate, passphrase.text);
    }
    throw std::runtime_error("No matching private key was found in the key library or this folder. Import the matching private key in SSH key manager, or place it beside this public key. For an encrypted OpenSSH/PEM key, use matching names (id_ed25519 and id_ed25519.pub, or server.pem and server.pub). A public key alone cannot sign in.");
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
std::wstring preparePrivateKey(HWND owner, const std::wstring &path, const char *supplied) {
    if (path.empty()) return path;
    int encrypted = 0, kind = wsKeyInspect(path.c_str(), &encrypted);
    if (kind == WS_KEY_INVALID) throw std::runtime_error(wsKeyError());
    if (kind == WS_KEY_PUBLIC) return resolvePublicKey(owner, path, supplied);
    if (kind == WS_KEY_PPK) return std::filesystem::absolute(path).wstring();
    Secret passphrase;
    if (encrypted && !readPassphrase(owner, path, supplied, passphrase)) return L"";
    Key key(wsKeyLoad(path.c_str(), passphrase.text), wsKeyFree);
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
