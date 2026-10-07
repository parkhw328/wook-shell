#pragma once
#include "store.h"
#include <string>
#include <string_view>
#include <vector>

namespace wook {
std::string utf8(const std::wstring &value);
std::wstring wide(const std::string &value);
std::wstring quoteArg(const std::wstring &value);
std::wstring trim(std::wstring value);
std::wstring executableDirectory();

struct Endpoint {
    std::wstring host, user, protocol = L"ssh";
    int port = 22;
};
Endpoint parseEndpoint(std::wstring input);

struct Profile {
    std::wstring name, host, user, protocol = L"ssh", keyFile, group;
    int port = 22;
    int fontSize = 9;
    bool passwordSaved = false;
    std::wstring alias, tabColor;
    std::wstring displayName() const { return alias.empty() ? name : alias; }
};
enum class PasswordAction { keep, replace, forget };
void applyTheme(WsStore *store, int fontSize = 9);
void initializeDefaults();
bool loadNavigationVisible();
void saveNavigationVisible(bool visible);
std::vector<Profile> loadProfiles();
void saveProfile(const Profile &profile, const std::wstring &originalName = L"",
                 PasswordAction passwordAction = PasswordAction::keep, std::wstring_view password = {});
void deleteProfile(const std::wstring &name);
void validateProfile(const Profile &profile);
int defaultPort(const std::wstring &protocol);
}
