#pragma once
#include "core.hpp"
#include "credentials.h"
#include <string_view>

namespace wook {
inline constexpr char passwordField[] = "WookSshPasswordDPAPI";
inline constexpr char passwordScopeField[] = "WookSshPasswordScope";
bool hasSavedPassword(const WsStore *store, const Profile &profile);
void updateSavedPassword(WsStore *store, const Profile &profile, PasswordAction action, std::wstring_view password);
bool isCredentialField(std::string_view key);
}
