#include "core.hpp"
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void check(bool ok, const char *name) {
    ++checks; if (!ok) throw std::runtime_error(name);
}
template<typename Fn> static void rejects(Fn fn, const char *name) {
    try { fn(); } catch (const std::exception &) { check(true, name); return; }
    check(false, name);
}
int wmain() {
    try {
        auto data = wook::executableDirectory() + L"\\test-data-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
        auto endpoint = wook::parseEndpoint(L" ssh://deploy@[2001:db8::1]:2222 ");
        check(endpoint.user == L"deploy" && endpoint.host == L"2001:db8::1" && endpoint.port == 2222, "IPv6 parsing");
        check(wook::parseEndpoint(L"telnet://router").port == 23, "protocol default");
        check(wook::parseEndpoint(L"::1").host == L"::1", "bare IPv6");
        check(wook::parseEndpoint(L"alice@example.com").user == L"alice", "SSH username");
        for (const auto *bad : {L"", L"-proxycmd", L"example.com:0", L"example.com:65536", L"host:x", L"ssh://[::1", L"ssh://host/path", L"https://host", L"host name"})
            rejects([&] { wook::parseEndpoint(bad); }, "reject malformed endpoint");
        std::vector<std::wstring> args{L"", L"simple", L"has spaces", L"C:\\한글 폴더\\key.ppk", L"trailing\\", L"slash\\\"quote", L"& | $(anything)"};
        std::wstring cmd = L"test.exe";
        for (auto &arg : args) cmd += L" " + wook::quoteArg(arg);
        int count; wchar_t **parsed = CommandLineToArgvW(cmd.c_str(), &count);
        check(count == (int)args.size() + 1, "quoted argument count");
        for (int i = 1; i < count; ++i) check(parsed[i] == args[i - 1], "Windows quoting round trip");
        LocalFree(parsed);
        check(wook::wide(wook::utf8(L"한글 서버 🚀")) == L"한글 서버 🚀", "Unicode round trip");
        wook::initializeDefaults();
        wook::Profile profile; profile.name = L"서울 production"; profile.host = L"127.0.0.1";
        profile.user = L"deploy"; profile.group = L"운영"; profile.keyFile = L"C:\\한글 키\\test.ppk";
        wook::saveProfile(profile);
        auto saved = wook::loadProfiles();
        check(saved.size() == 1 && saved[0].name == profile.name && saved[0].keyFile == profile.keyFile, "Unicode profile persistence");
        rejects([&] { wook::saveProfile(profile); }, "duplicate profile must not overwrite");
        wchar_t *path = wsPath(L"sessions", wook::utf8(profile.name).c_str());
        WsStore *writer = wsOpen(path, 1);
        check(writer != nullptr, "open writer");
        check(wsSet(writer, "PortForwardings", "L8080=localhost:80") && wsSave(writer), "preserve advanced tunnel configuration");
        WsStore *blocked = wsOpen(path, 1);
        check(blocked == nullptr, "exclusive writer lock");
        WsStore *reader = wsOpen(path, 0);
        check(reader && std::string(wsGet(reader, "PortForwardings")) == "L8080=localhost:80", "read committed value while writer is open");
        wsClose(reader); wsClose(writer);
        auto oldName = profile.name;
        profile.name = L"Renamed host"; profile.port = 2222;
        wook::saveProfile(profile, oldName);
        saved = wook::loadProfiles();
        check(saved.size() == 1 && saved[0].port == 2222, "rename and update");
        check(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES, "rename removes previous record");
        free(path);
        path = wsPath(L"sessions", wook::utf8(profile.name).c_str());
        reader = wsOpen(path, 0);
        check(reader && std::string(wsGet(reader, "PortForwardings")) == "L8080=localhost:80", "advanced values survive host editing");
        check(std::string(wsGet(reader, "TrueColour")) == "1" && std::string(wsGet(reader, "Font")) == "JetBrains Mono", "terminal defaults");
        wsClose(reader);
        { std::ofstream f(std::filesystem::path(path), std::ios::binary); f << "WS1\nnot-hex=broken\n"; }
        check(wsOpen(path, 0) == nullptr, "malformed file fails closed");
        rejects([&] { wook::saveProfile(profile, profile.name); }, "corrupt record is not silently overwritten");
        free(path);
        check(wsPath(L"../outside", "x") == nullptr, "reject path traversal category");
        check(wsPath(L"sessions", std::string(101, 'x').c_str()) == nullptr, "bounded session filenames");
        std::wcout << L"PASS: " << checks << L" checks. Isolated data: " << data << L"\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}
