#include "credentials.hpp"
#include <wincrypt.h>
#include <memory>
#include <stdexcept>

namespace {
constexpr size_t maxPassword = 1024, maxBlob = 64 * 1024;
struct ProtectedData {
    DATA_BLOB blob{};
    ~ProtectedData() { if (blob.pbData) { SecureZeroMemory(blob.pbData, blob.cbData); LocalFree(blob.pbData); } }
};
std::string scope(const std::string &host, int port, const std::string &user) {
    auto normalized = host;
    for (char &c : normalized) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return "wShell SSH password v1\n" + std::to_string(normalized.size()) + ":" + normalized + "\n" +
        std::to_string(port) + "\n" + std::to_string(user.size()) + ":" + user;
}
std::string scope(const wook::Profile &p) { return scope(wook::utf8(p.host), p.port, wook::utf8(p.user)); }
std::string encode(const DATA_BLOB &blob) {
    DWORD length = 0;
    if (!CryptBinaryToStringA(blob.pbData, blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length))
        throw std::runtime_error("Cannot encode the encrypted password.");
    std::string text(length, '\0');
    if (!CryptBinaryToStringA(blob.pbData, blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &length))
        throw std::runtime_error("Cannot encode the encrypted password.");
    text.resize(length); return text;
}
std::vector<BYTE> decode(const char *text) {
    size_t length = strlen(text); DWORD size = 0;
    if (!length || length > maxBlob || !CryptStringToBinaryA(text, (DWORD)length, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                                                          nullptr, &size, nullptr, nullptr) || size > maxBlob)
        throw std::runtime_error("Invalid encrypted password.");
    std::vector<BYTE> bytes(size);
    if (!CryptStringToBinaryA(text, (DWORD)length, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, bytes.data(), &size, nullptr, nullptr))
        throw std::runtime_error("Invalid encrypted password.");
    return bytes;
}
}
namespace wook {
bool isCredentialField(std::string_view key) {
    return key == "ProxyPassword" || key == passwordField || key == passwordScopeField;
}
bool hasSavedPassword(const WsStore *store, const Profile &p) {
    auto encrypted = wsGet(store, passwordField), binding = wsGet(store, passwordScopeField);
    return p.protocol == L"ssh" && p.keyFile.empty() && !p.user.empty() && encrypted && *encrypted && binding && binding == scope(p);
}
void updateSavedPassword(WsStore *store, const Profile &p, PasswordAction action, std::wstring_view password) {
    if (p.protocol != L"ssh" || !p.keyFile.empty()) action = PasswordAction::forget;
    if (action == PasswordAction::keep && !hasSavedPassword(store, p)) action = PasswordAction::forget;
    if (action == PasswordAction::keep) return;
    if (action == PasswordAction::forget) {
        wsSet(store, passwordField, ""); wsSet(store, passwordScopeField, ""); return;
    }
    if (p.user.empty()) throw std::runtime_error("Enter a username before saving an SSH password.");
    if (password.empty() || password.size() > maxPassword || password.find(L'\0') != std::wstring_view::npos)
        throw std::runtime_error("Enter a password of 1 to 1024 characters.");
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, password.data(), (int)password.size(), nullptr, 0, nullptr, nullptr))
        throw std::runtime_error("The password contains invalid Unicode.");
    auto binding = scope(p);
    DATA_BLOB input{(DWORD)(password.size() * sizeof(wchar_t)), (BYTE *)password.data()};
    DATA_BLOB entropy{(DWORD)binding.size(), (BYTE *)binding.data()};
    ProtectedData output;
    if (!CryptProtectData(&input, L"wShell SSH password", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.blob))
        throw std::runtime_error("Windows could not encrypt the password. The host was not saved.");
    auto ciphertext = encode(output.blob);
    if (!wsSet(store, passwordField, ciphertext.c_str()) || !wsSet(store, passwordScopeField, binding.c_str()))
        throw std::runtime_error("Cannot stage the encrypted password.");
}
}
extern "C" int wsLoadSavedPassword(const char *session, const char *host, int port, const char *user, char **password) {
    if (!password) return -1;
    *password = nullptr;
    try {
        if (!session || !*session || !host || !user || !*user) return 0;
        wchar_t *path = wsPath(L"sessions", session);
        std::unique_ptr<WsStore, decltype(&wsClose)> store(wsOpen(path, 0), wsClose); free(path);
        if (!store) return -1;
        auto encrypted = wsGet(store.get(), wook::passwordField), binding = wsGet(store.get(), wook::passwordScopeField);
        if (!encrypted || !*encrypted || !binding || binding != scope(host, port, user)) return 0;
        auto bytes = decode(encrypted);
        DATA_BLOB input{(DWORD)bytes.size(), bytes.data()}, entropy{(DWORD)strlen(binding), (BYTE *)binding};
        ProtectedData output;
        if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.blob)) return -1;
        if (!output.blob.cbData || output.blob.cbData % sizeof(wchar_t) || output.blob.cbData > maxPassword * sizeof(wchar_t)) return -1;
        auto wide = (wchar_t *)output.blob.pbData; int length = output.blob.cbData / sizeof(wchar_t);
        for (int i = 0; i < length; ++i) if (!wide[i]) return -1;
        int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, nullptr, 0, nullptr, nullptr);
        if (!count) return -1;
        char *text = (char *)calloc((size_t)count + 1, 1); if (!text) return -1;
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, text, count, nullptr, nullptr)) { wsPasswordFree(text); return -1; }
        *password = text; return 1;
    } catch (...) { return -1; }
}
extern "C" void wsPasswordFree(char *password) {
    if (password) { SecureZeroMemory(password, strlen(password)); free(password); }
}
