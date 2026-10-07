#pragma once
#include "store.h"
#include <string>
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
    int fontSize = 11;
};
void applyTheme(WsStore *store, int fontSize = 11);
void initializeDefaults();
std::vector<Profile> loadProfiles();
void saveProfile(const Profile &profile, const std::wstring &originalName = L"");
void deleteProfile(const std::wstring &name);
void validateProfile(const Profile &profile);
int defaultPort(const std::wstring &protocol);
}
