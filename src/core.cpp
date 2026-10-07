#include "core.hpp"
#include <algorithm>
#include <cwctype>
#include <memory>
#include <stdexcept>

namespace wook {
std::string utf8(const std::wstring &value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode text.");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string &value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 in saved settings.");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), count);
    return result;
}
std::wstring quoteArg(const std::wstring &value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
std::wstring trim(std::wstring value) {
    auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring executableDirectory() {
    std::wstring path(32768, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    if (!size || size >= path.size()) throw std::runtime_error("Cannot locate the application folder.");
    path.resize(size);
    return path.substr(0, path.find_last_of(L"\\/"));
}
int defaultPort(const std::wstring &protocol) {
    if (protocol == L"telnet") return 23;
    if (protocol == L"rlogin") return 513;
    if (protocol == L"serial") return 9600;
    return 22;
}
static int parsePort(const std::wstring &text) {
    if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos || text.size() > 5)
        throw std::runtime_error("Port must be a number from 1 to 65535.");
    int port = std::stoi(text);
    if (port < 1 || port > 65535) throw std::runtime_error("Port must be a number from 1 to 65535.");
    return port;
}
Endpoint parseEndpoint(std::wstring input) {
    input = trim(input);
    Endpoint out;
    auto scheme = input.find(L"://");
    if (scheme != std::wstring::npos) { out.protocol = input.substr(0, scheme); input.erase(0, scheme + 3); }
    if (out.protocol != L"ssh" && out.protocol != L"telnet" && out.protocol != L"rlogin" && out.protocol != L"raw")
        throw std::runtime_error("Use ssh://, telnet://, rlogin://, or raw://. Add serial connections with New host.");
    out.port = defaultPort(out.protocol);
    auto at = input.rfind(L'@');
    if (at != std::wstring::npos) { out.user = input.substr(0, at); input.erase(0, at + 1); }
    if (!input.empty() && input.front() == L'[') {
        auto close = input.find(L']');
        if (close == std::wstring::npos) throw std::runtime_error("Close the IPv6 address with ].");
        out.host = input.substr(1, close - 1);
        auto rest = input.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != L':') throw std::runtime_error("Use [IPv6 address]:port.");
            out.port = parsePort(rest.substr(1));
        }
    } else {
        auto colon = input.find(L':');
        if (colon != std::wstring::npos && colon == input.rfind(L':')) {
            out.port = parsePort(input.substr(colon + 1)); input.resize(colon);
        }
        out.host = input;
    }
    Profile p; p.name = L"Quick connect"; p.host = out.host; p.user = out.user; p.port = out.port; p.protocol = out.protocol;
    validateProfile(p);
    return out;
}
void validateProfile(const Profile &p) {
    if (trim(p.name).empty() || utf8(p.name).size() > 100 || p.name == L"Default Settings" || p.name.starts_with(L"__wook_"))
        throw std::runtime_error("Choose a host name of 1-100 UTF-8 bytes, excluding reserved names.");
    if (p.host.empty() || p.host.size() > 253 || p.host.front() == L'-' || p.host.find_first_of(L" \t\r\n/\\\"@[]") != std::wstring::npos)
        throw std::runtime_error("Enter a hostname, IP address, or serial port such as COM3.");
    if (p.user.find_first_of(L"\r\n\t \"@:") != std::wstring::npos || p.user.size() > 128)
        throw std::runtime_error("The username contains unsupported characters.");
    if (p.protocol != L"ssh" && p.protocol != L"telnet" && p.protocol != L"rlogin" && p.protocol != L"raw" && p.protocol != L"serial")
        throw std::runtime_error("Select a supported protocol.");
    if (p.protocol == L"serial") {
        if (p.port < 50 || p.port > 4000000) throw std::runtime_error("Serial speed must be 50-4000000 baud.");
    } else if (p.port < 1 || p.port > 65535) throw std::runtime_error("Port must be 1-65535.");
    if (p.fontSize < 8 || p.fontSize > 32) throw std::runtime_error("Font size must be 8-32 points.");
}
using StorePtr = std::unique_ptr<WsStore, decltype(&wsClose)>;
static StorePtr profileStore(const std::wstring &name, bool write) {
    auto encoded = utf8(name);
    wchar_t *path = wsPath(L"sessions", encoded.c_str());
    WsStore *raw = wsOpen(path, write);
    free(path);
    if (!raw) throw std::runtime_error("Cannot read or lock session data. Check folder permissions or restore a valid backup.");
    return StorePtr(raw, wsClose);
}
void applyTheme(WsStore *s, int fontSize) {
    const std::pair<const char *, const char *> settings[] = {
        {"Font", "JetBrains Mono"}, {"FontIsBold", "0"}, {"FontCharSet", "1"}, {"FontQuality", "3"},
        {"LineCodePage", "UTF-8"}, {"TerminalType", "xterm-256color"}, {"Environment", "COLORTERM=truecolor"},
        {"ANSIColour", "1"}, {"Xterm256Colour", "1"}, {"TrueColour", "1"}, {"BoldAsColour", "0"},
        {"ScrollbackLines", "10000"}, {"ScrollBar", "0"}, {"WindowBorder", "14"}, {"CursorType", "0"},
        {"BlinkCur", "1"}, {"BellType", "0"}, {"CloseOnExit", "1"}, {"WarnOnClose", "0"},
        {"TerminalWidth", "110"}, {"TerminalHeight", "32"}, {"NoRemoteResize", "1"},
        {"NoRemoteWinTitle", "1"}, {"NoRemoteClearScroll", "1"}, {"TCPKeepalives", "1"},
        {"PingInterval", "0"}, {"PingIntervalSecs", "30"}, {"CtrlShiftCV", "explicit"}, {"Present", "1"},
        {"TryAgent", "0"}, {"AgentFwd", "0"}, {"ConnectionSharing", "0"}
    };
    for (auto [key, value] : settings) wsSet(s, key, value);
    wsSet(s, "FontHeight", std::to_string(fontSize).c_str());
    const char *colors[] = {
        "206,205,195", "255,252,240", "16,15,15", "28,27,26", "16,15,15", "58,169,159",
        "40,39,38", "87,86,83", "175,48,41", "209,77,65", "102,128,11", "135,154,57",
        "173,131,1", "208,162,21", "32,94,166", "67,133,190", "160,47,111", "206,93,151",
        "36,131,123", "58,169,159", "206,205,195", "255,252,240"
    };
    for (size_t i = 0; i < std::size(colors); ++i) wsSet(s, ("Colour" + std::to_string(i)).c_str(), colors[i]);
}
void initializeDefaults() {
    auto s = profileStore(L"Default Settings", true);
    if (!s->exists) {
        applyTheme(s.get());
        if (!wsSave(s.get())) throw std::runtime_error("The application folder must be writable for portable settings.");
    }
}
std::vector<Profile> loadProfiles() {
    size_t count = 0;
    char **names = wsList(L"sessions", &count);
    std::vector<Profile> profiles;
    try {
        for (size_t i = 0; i < count; ++i) {
            auto name = wide(names[i]);
            if (name == L"Default Settings" || name.starts_with(L"__wook_")) continue;
            auto s = profileStore(name, false);
            auto read = [&](const char *key, const char *fallback = "") { auto value = wsGet(s.get(), key); return wide(value ? value : fallback); };
            Profile p; p.name = name; p.host = read("HostName"); p.user = read("UserName");
            p.protocol = read("Protocol", "ssh"); p.keyFile = read("PublicKeyFile"); p.group = read("WookGroup");
            if (p.protocol == L"serial") p.host = read("SerialLine");
            try { p.port = std::stoi(read(p.protocol == L"serial" ? "SerialSpeed" : "PortNumber", "22")); } catch (...) { p.port = defaultPort(p.protocol); }
            try { p.fontSize = std::stoi(read("FontHeight", "11")); } catch (...) { p.fontSize = 11; }
            if (!p.host.empty()) profiles.push_back(std::move(p));
        }
    } catch (...) { wsFreeList(names, count); throw; }
    wsFreeList(names, count);
    std::sort(profiles.begin(), profiles.end(), [](const auto &a, const auto &b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return profiles;
}
void saveProfile(const Profile &p, const std::wstring &originalName) {
    validateProfile(p);
    auto s = profileStore(p.name, true);
    bool rename = !originalName.empty() && originalName != p.name;
    if (s->exists && (originalName.empty() || rename)) throw std::runtime_error("A saved host already has this name.");
    if (rename) {
        auto old = profileStore(originalName, false);
        for (auto pair = old->pairs; pair; pair = pair->next) wsSet(s.get(), pair->key, pair->value);
    }
    if (!s->exists && !rename) applyTheme(s.get(), p.fontSize);
    auto set = [&](const char *key, const std::wstring &value) { auto text = utf8(value); wsSet(s.get(), key, text.c_str()); };
    set("HostName", p.protocol == L"serial" ? L"" : p.host); set("UserName", p.user); set("Protocol", p.protocol);
    set("PublicKeyFile", p.keyFile); set("WookGroup", p.group);
    set("PortNumber", std::to_wstring(p.protocol == L"serial" ? 22 : p.port));
    set("FontHeight", std::to_wstring(p.fontSize));
    if (p.protocol == L"serial") { set("SerialLine", p.host); set("SerialSpeed", std::to_wstring(p.port)); }
    if (!wsSave(s.get())) throw std::runtime_error("Could not save the host. Check disk space and folder permissions.");
    s.reset();
    if (rename) deleteProfile(originalName);
}
void deleteProfile(const std::wstring &name) {
    wchar_t *path = wsPath(L"sessions", utf8(name).c_str());
    int ok = wsRemove(path); free(path);
    if (!ok) throw std::runtime_error("Could not delete the host.");
}
}
