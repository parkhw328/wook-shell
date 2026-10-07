#include "launch.hpp"
#include "credentials.hpp"
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace wook {
LaunchRequest parseLaunchArguments(std::span<const std::wstring_view> args) {
    LaunchRequest result;
    if (args.empty()) return result;
    if (args.size() == 1 && (args[0] == L"--help" || args[0] == L"-h" || args[0] == L"-?")) {
        result.mode = LaunchRequest::Mode::help; return result;
    }
    if (args.size() == 1 && args[0] == L"--preview") {
        result.mode = LaunchRequest::Mode::preview; return result;
    }
    std::wstring host, user, port;
    bool hasHost = false, hasUser = false, hasPort = false, hasPassword = false, ssh = false;
    auto once = [](bool &seen) { if (seen) throw std::runtime_error("An option was supplied more than once. Use --help for usage."); seen = true; };
    for (size_t i = 0; i < args.size(); ++i) {
        auto option = args[i];
        auto value = [&]() {
            if (++i == args.size() || args[i].empty()) throw std::runtime_error("An option is missing its value. Use --help for usage.");
            return args[i];
        };
        if (option == L"-ssh" || option == L"--ssh") once(ssh);
        else if (option == L"--host") { once(hasHost); host = value(); }
        else if (option == L"-l" || option == L"--user") { once(hasUser); user = value(); }
        else if (option == L"-P" || option == L"--port") { once(hasPort); port = value(); }
        else if (option == L"--name") { once(result.named); result.profile.name = value(); }
        else if (option == L"-pw" || option == L"--password") { once(hasPassword); auto p = value(); result.password.assign(p.begin(), p.end()); }
        else if (option == L"--password-stdin") { once(hasPassword); result.passwordStdin = true; }
        else if (!option.starts_with(L"-")) { once(hasHost); host = option; }
        else throw std::runtime_error("Unsupported launch option. Use --help for usage."); // Never echo possible secrets.
    }
    if (!hasHost) throw std::runtime_error("Supply an SSH host address. Use --help for usage.");
    auto endpoint = parseEndpoint(host);
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
