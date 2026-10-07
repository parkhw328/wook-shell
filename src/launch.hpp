#pragma once
#include "core.hpp"
#include <span>

namespace wook {
struct LaunchRequest {
    enum class Mode { workspace, connect, preview, help } mode = Mode::workspace;
    Profile profile;
    bool named = false, passwordStdin = false;
    std::vector<wchar_t> password;
    LaunchRequest() = default;
    LaunchRequest(const LaunchRequest &) = delete;
    LaunchRequest &operator=(const LaunchRequest &) = delete;
    LaunchRequest(LaunchRequest &&) = default;
    ~LaunchRequest() { clearPassword(); }
    void clearPassword() { if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t)); password.clear(); }
};
LaunchRequest parseLaunchArguments(std::span<const std::wstring_view> arguments);
LaunchRequest parseLaunchCommand(const wchar_t *commandLine);
void readLaunchPassword(LaunchRequest &request);
Profile resolveLaunchProfile(const LaunchRequest &request);

class LaunchPassword {
    std::wstring name_;
public:
    LaunchPassword(const Profile &profile, std::wstring_view password);
    LaunchPassword(const LaunchPassword &) = delete;
    ~LaunchPassword();
    const std::wstring &name() const { return name_; }
};
inline constexpr wchar_t launchHelp[] =
    L"Open an SSH connection and save a new host when needed:\n\n"
    L"wShell.exe -ssh user@host -P 22 -pw \"password\"\n"
    L"wShell.exe --host 192.0.2.10 --user deploy --name \"My server\"\n\n"
    L"Options: -ssh / --ssh, --host, -P / --port, -l / --user,\n"
    L"-pw / --password, --password-stdin, --name, --help\n\n"
    L"Passwords apply to this connection only. Command-line passwords may\n"
    L"appear in process listings or shell history; prefer --password-stdin\n"
    L"with a UTF-8 pipe, or omit the password and enter it in the terminal.\n"
    L"Unknown server keys still require confirmation.";
}
