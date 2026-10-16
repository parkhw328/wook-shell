#include "launch.hpp"
#include "credentials.hpp"
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace wook {
namespace {
// Decode directly into a scrubbed buffer: URL credentials must not survive in
// ordinary string temporaries, including on malformed UTF-8 exceptions.
struct UrlText {
    std::vector<char> bytes;
    std::vector<wchar_t> text;
    ~UrlText() {
        if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
        if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
    }
    void decode(std::wstring_view input) {
        bytes.reserve(input.size() * 4);
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            throw std::runtime_error("Invalid URL escape.");
        };
        for (size_t i = 0; i < input.size(); ++i) {
            if (input[i] == L'%') {
                if (i + 2 >= input.size()) throw std::runtime_error("Invalid URL escape.");
                bytes.push_back(char(hex(input[i + 1]) * 16 + hex(input[i + 2]))); i += 2;
            } else {
                wchar_t chars[2] = {input[i] == L'+' ? L' ' : input[i], 0};
                int length = 1;
                if (chars[0] >= 0xD800 && chars[0] <= 0xDBFF && i + 1 < input.size()) chars[length++] = input[++i];
                char encoded[4]{};
                int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, chars, length, encoded, 4, nullptr, nullptr);
                SecureZeroMemory(chars, sizeof(chars));
                if (!count) throw std::runtime_error("Invalid Unicode in URL.");
                bytes.insert(bytes.end(), encoded, encoded + count); SecureZeroMemory(encoded, sizeof(encoded));
            }
        }
        if (bytes.empty()) return;
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0);
        if (!count || std::find(bytes.begin(), bytes.end(), '\0') != bytes.end()) throw std::runtime_error("Invalid UTF-8 in URL.");
        text.resize(count);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), text.data(), count);
    }
};
std::wstring urlValue(std::wstring_view value) {
    UrlText decoded; decoded.decode(value); return {decoded.text.begin(), decoded.text.end()};
}
}

LaunchRequest parseLaunchArguments(std::span<const std::wstring_view> args) {
    LaunchRequest result;
    if (args.empty()) return result;
    if (args.size() == 1 && (args[0] == L"--help" || args[0] == L"-h" || args[0] == L"-?")) {
        result.mode = LaunchRequest::Mode::help; return result;
    }
    if (args.size() == 1 && args[0] == L"--preview") {
        result.mode = LaunchRequest::Mode::preview; return result;
    }
    std::wstring_view host;
    std::wstring user, port;
    bool hasPath = false, hasFiles = false;
    bool hasHost = false, hasUser = false, hasPort = false, hasPassword = false, ssh = false;
    auto once = [](bool &seen) { if (seen) throw std::runtime_error("An option was supplied more than once. Use --help for usage."); seen = true; };
    for (size_t i = 0; i < args.size(); ++i) {
        auto option = args[i];
        auto value = [&]() {
            if (++i == args.size() || args[i].empty()) throw std::runtime_error("An option is missing its value. Use --help for usage.");
            return args[i];
        };
        if (option == L"--sftp" || option == L"-sftp") { once(hasFiles); result.files = true; }
        else if (option == L"--remote-path") { once(hasPath); result.remotePath = value(); }
        else if (option.starts_with(L"/username=") || option.starts_with(L"/password=") || option.starts_with(L"/sessionname=")) {
            result.files = true;
            auto equal = option.find(L'='); auto content = option.substr(equal + 1);
            if (content.empty()) throw std::runtime_error("An option is missing its value.");
            if (option.starts_with(L"/username=")) { once(hasUser); user = content; }
            else if (option.starts_with(L"/password=")) { once(hasPassword); result.password.assign(content.begin(), content.end()); }
            else { once(result.named); result.profile.name = content; }
        }
        else if (option == L"-ssh" || option == L"--ssh") once(ssh);
        else if (option == L"--host") { once(hasHost); host = value(); }
        else if (option == L"-l" || option == L"--user") { once(hasUser); user = value(); }
        else if (option == L"-P" || option == L"--port") { once(hasPort); port = value(); }
        else if (option == L"--name") { once(result.named); result.profile.name = value(); }
        else if (option == L"-pw" || option == L"--password") { once(hasPassword); auto p = value(); result.password.assign(p.begin(), p.end()); }
        else if (option == L"--password-stdin") { once(hasPassword); result.passwordStdin = true; }
        else if (!option.starts_with(L"-") && !option.starts_with(L"/")) { once(hasHost); host = option; }
        else throw std::runtime_error("Unsupported launch option. Use --help for usage."); // Never echo possible secrets.
    }
    if (!hasHost) throw std::runtime_error("Supply an SSH host address. Use --help for usage.");
    Endpoint endpoint;
    if (host.starts_with(L"sftp://") || host.starts_with(L"winscp-sftp://")) {
        result.files = true;
        auto url = host.substr(host.find(L"://") + 3);
        if (url.find_first_of(L";?#") != std::wstring_view::npos) throw std::runtime_error("URL advanced settings, queries and fragments are not supported.");
        auto slash = url.find(L'/');
        auto authority = url.substr(0, slash);
        if (slash != std::wstring_view::npos) {
            once(hasPath);
            if (!url.ends_with(L"/")) throw std::runtime_error("Use a directory URL ending in /. File URL downloads are not supported.");
            result.remotePath = urlValue(url.substr(slash));
        }
        auto at = authority.find(L'@');
        if (at != std::wstring_view::npos) {
            auto credentials = authority.substr(0, at);
            auto colon = credentials.find(L':');
            once(hasUser); user = urlValue(credentials.substr(0, colon));
            if (colon != std::wstring_view::npos) {
                once(hasPassword);
                UrlText decoded; decoded.decode(credentials.substr(colon + 1));
                result.password = std::move(decoded.text);
                if (result.password.empty()) throw std::runtime_error("Supply a nonempty URL password or omit it.");
            }
            authority.remove_prefix(at + 1);
        }
        if (authority.find_first_of(L"@%") != std::wstring_view::npos) throw std::runtime_error("Invalid SFTP URL authority.");
        endpoint = parseEndpoint(std::wstring(authority));
    } else endpoint = parseEndpoint(std::wstring(host));
    if (ssh && result.files) throw std::runtime_error("Choose either SSH terminal or SFTP launch mode.");
    if (hasPath && !result.files) throw std::runtime_error("Remote paths require SFTP mode.");
    if (result.remotePath.size() > 32768 || result.remotePath.find_first_of(L"\r\n") != std::wstring::npos ||
        result.remotePath.find(L'\0') != std::wstring::npos) throw std::runtime_error("Invalid remote directory.");
    if (endpoint.protocol != L"ssh") throw std::runtime_error("Launch arguments currently support SSH connections only.");
    if (hasUser && !endpoint.user.empty() && user != endpoint.user)
        throw std::runtime_error("The endpoint and user option specify different accounts.");
    auto &p = result.profile;
    p.host = endpoint.host; p.user = hasUser ? user : endpoint.user; p.port = endpoint.port;
    if (hasPort) {
        if (port.size() > 5 || port.find_first_not_of(L"0123456789") != std::wstring::npos)
            throw std::runtime_error("Port must be a number from 1 to 65535.");
        p.port = std::stoi(port);
    }
    if (!result.named) p.name = L"SSH connection";
    if (p.name.find_first_of(L"\r\n\t") != std::wstring::npos) throw std::runtime_error("The host name must be on one line.");
    validateProfile(p);
    if (hasPassword && p.user.empty()) throw std::runtime_error("Supply a username when passing a password.");
    if (result.password.size() > 1024) throw std::runtime_error("The password must contain at most 1024 characters.");
    if (!result.password.empty() && (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, result.password.data(), (int)result.password.size(), nullptr, 0, nullptr, nullptr) ||
        std::find(result.password.begin(), result.password.end(), L'\0') != result.password.end()))
        throw std::runtime_error("The password contains invalid Unicode.");
    result.mode = LaunchRequest::Mode::connect;
    return result;
}
LaunchRequest parseLaunchCommand(const wchar_t *commandLine) {
    struct Arguments {
        int count = 0;
        wchar_t **values = nullptr;
        ~Arguments() {
            if (values) { for (int i = 0; i < count; ++i) SecureZeroMemory(values[i], wcslen(values[i]) * sizeof(wchar_t)); LocalFree(values); }
        }
    } parsed;
    parsed.values = CommandLineToArgvW(commandLine, &parsed.count);
    if (!parsed.values) throw std::runtime_error("Cannot parse launch arguments.");
    std::vector<std::wstring_view> args;
    for (int i = 1; i < parsed.count; ++i) args.emplace_back(parsed.values[i]);
    return parseLaunchArguments(args);
}
void readLaunchPassword(LaunchRequest &request) {
    if (!request.passwordStdin) return;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!input || input == INVALID_HANDLE_VALUE || GetFileType(input) != FILE_TYPE_PIPE)
        throw std::runtime_error("--password-stdin requires a UTF-8 pipe ending with a newline or EOF.");
    struct Buffer { char bytes[4097]{}; ~Buffer() { SecureZeroMemory(bytes, sizeof(bytes)); } } buffer;
    size_t length = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(input, buffer.bytes + length, 1, &read, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;
            throw std::runtime_error("Cannot read the password pipe.");
        }
        if (!read || buffer.bytes[length] == '\n') break;
        if (++length == sizeof(buffer.bytes) - 1) throw std::runtime_error("Password input is too long.");
    }
    if (length && buffer.bytes[length - 1] == '\r') --length;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buffer.bytes, (int)length, nullptr, 0);
    if (count < 1 || count > 1024 || memchr(buffer.bytes, 0, length)) throw std::runtime_error("Supply a UTF-8 password of 1 to 1024 characters.");
    request.password.resize(count);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buffer.bytes, (int)length, request.password.data(), count);
}
Profile resolveLaunchProfile(const LaunchRequest &request) {
    auto profiles = loadProfiles();
    const auto &p = request.profile;
    auto sameHost = [](const std::wstring &a, const std::wstring &b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
    };
    auto sameEndpoint = [&](const Profile &saved) {
        return saved.protocol == L"ssh" && sameHost(saved.host, p.host) && saved.port == p.port && saved.user == p.user;
    };
    if (request.named) for (const auto &saved : profiles) if (sameHost(saved.name, p.name)) {
        if (!sameEndpoint(saved)) throw std::runtime_error("That saved host name belongs to another address, port or account. Choose a different --name.");
        return saved;
    }
    for (const auto &saved : profiles) if (sameEndpoint(saved)) return saved;
    Profile created = p;
    if (!request.named) {
        auto base = (p.user.empty() ? L"" : p.user + L"@") + (p.host.find(L':') == std::wstring::npos ? p.host : L"[" + p.host + L"]");
        if (p.port != 22) base += L":" + std::to_wstring(p.port);
        if (utf8(base).size() > 80) base = L"SSH connection";
        created.name = base;
        for (int suffix = 2; std::any_of(profiles.begin(), profiles.end(), [&](const auto &saved) { return sameHost(saved.name, created.name); }); ++suffix)
            created.name = base + L" (" + std::to_wstring(suffix) + L")";
    }
    saveProfile(created);
    return created;
}
LaunchPassword::LaunchPassword(const Profile &profile, std::wstring_view password) {
    static std::atomic<unsigned> sequence = 0;
    name_ = L"__wook_launch_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(++sequence);
    auto path = wsPath(L"sessions", utf8(name_).c_str());
    std::unique_ptr<WsStore, decltype(&wsClose)> store(wsOpen(path, true), wsClose); free(path);
    if (!store || store->exists) throw std::runtime_error("Cannot create the one-use password record.");
    auto binding = profile; binding.keyFile.clear();
    updateSavedPassword(store.get(), binding, PasswordAction::replace, password);
    if (!wsSave(store.get())) throw std::runtime_error("Cannot encrypt the one-use password.");
}
LaunchPassword::~LaunchPassword() {
    auto path = wsPath(L"sessions", utf8(name_).c_str()); wsRemove(path); free(path);
}
}
