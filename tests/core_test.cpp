#include "core.hpp"
#include "backup.hpp"
#include "credentials.hpp"
#include "split.hpp"
#include <algorithm>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

static int checks = 0;
static void check(bool ok, const char *name) {
    ++checks; if (!ok) throw std::runtime_error(name);
}
template<typename Fn> static void rejects(Fn fn, const char *name) {
    try { fn(); } catch (const std::exception &) { check(true, name); return; }
    check(false, name);
}
#include "password_test.inc"
int wmain() {
    try {
        for (int count=1;count<=4;++count) for(int width:{421,800,1301}) {
            auto frames=wook::splitRects(count,13,17,width,611);
            check((int)frames.size()==count,"requested split pane count");
            for(size_t i=0;i<frames.size();++i) {
                auto a=frames[i];check(a.x>=13&&a.y>=17&&a.x+a.width<=13+width&&a.y+a.height<=628,"split panes bounded");
                check(a.width>100&&a.height>100,"usable split dimensions");
                for(size_t j=0;j<i;++j){auto b=frames[j];check(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y,"non-overlapping split panes");}
            }
        }
        auto data = wook::executableDirectory() + L"\\test-data-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
        auto endpoint = wook::parseEndpoint(L" ssh://deploy@[2001:db8::1]:2222 ");
        check(endpoint.user == L"deploy" && endpoint.host == L"2001:db8::1" && endpoint.port == 2222, "IPv6 parsing");
        check(wook::parseEndpoint(L"telnet://router").port == 23, "protocol default");
        check(wook::parseEndpoint(L"::1").host == L"::1", "bare IPv6");
        check(wook::parseEndpoint(L"alice@example.com").user == L"alice", "SSH username");
        for (const auto *bad : {L"", L"-proxycmd", L"example.com:0", L"example.com:65536", L"host:x", L"ssh://[::1", L"ssh://host/path", L"https://host", L"host name", L"ssh://alice:secret@host"})
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
        wsSet(writer, "ProxyPassword", "test-only-secret-must-not-export");
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
        auto backup = data + L"\\settings.wshell";
        wchar_t *trustPath = wsPath(L"trust", "hostkeys");
        writer = wsOpen(trustPath, 1); free(trustPath);
        wsSet(writer, "rsa@22:test", "source-host-key"); check(wsSave(writer), "seed export trust"); wsClose(writer);
        wook::exportSettings(backup);
        { std::ifstream in(std::filesystem::path(backup), std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)), {});
          check(bytes.find("test-only-secret-must-not-export") == std::string::npos, "export excludes proxy passwords"); }
        auto importedData = data + L"-import";
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", importedData.c_str());
        wook::initializeDefaults(); wook::importSettings(backup);
        saved = wook::loadProfiles();
        check(saved.size() == 1 && saved[0].name == profile.name && saved[0].port == 2222, "backup round trip");
        auto edited = saved[0]; edited.port = 3333; wook::saveProfile(edited, edited.name);
        trustPath = wsPath(L"trust", "hostkeys"); writer = wsOpen(trustPath, 1);
        wsSet(writer, "rsa@22:test", "existing-host-key"); check(wsSave(writer), "seed existing trust"); wsClose(writer);
        wook::importSettings(backup);
        check(wook::loadProfiles()[0].port == 3333, "import preserves existing session names");
        reader = wsOpen(trustPath, 0); free(trustPath);
        check(std::string(wsGet(reader, "rsa@22:test")) == "existing-host-key", "import cannot replace an existing trusted host key"); wsClose(reader);
        auto broken = data + L"\\broken.wshell";
        { std::ifstream in(std::filesystem::path(backup), std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)), {});
          bytes.pop_back(); std::ofstream out(std::filesystem::path(broken), std::ios::binary); out << bytes; }
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", (data + L"-invalid-import").c_str());
        rejects([&] { wook::importSettings(broken); }, "truncated backup is rejected");
        check(wook::loadProfiles().empty(), "invalid backup makes no partial changes");
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", importedData.c_str());
        auto extra = profile; extra.name = L"Legacy-only host"; extra.port = 4444;
        wook::saveProfile(extra);
        trustPath = wsPath(L"trust", "hostkeys"); writer = wsOpen(trustPath, 1); free(trustPath);
        check(writer && wsSet(writer, "ed25519@22:legacy", "legacy-host-key") && wsSave(writer), "seed legacy trust"); wsClose(writer);
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
        check(wook::migrateLegacySettings(importedData), "legacy folder migrates on first launch");
        saved = wook::loadProfiles();
        check(saved.size() == 2, "migration includes missing sessions");
        check(std::any_of(saved.begin(), saved.end(), [&](const auto &p) { return p.name == profile.name && p.port == 2222; }),
              "migration preserves destination sessions");
        reader = wsOpen(path, 0);
        check(std::string(wsGet(reader, "PortForwardings")) == "L8080=localhost:80", "migration preserves advanced settings"); wsClose(reader);
        trustPath = wsPath(L"trust", "hostkeys"); reader = wsOpen(trustPath, 0); free(trustPath);
        check(std::string(wsGet(reader, "rsa@22:test")) == "source-host-key", "migration preserves destination trust");
        check(wsGet(reader, "ed25519@22:legacy") && std::string(wsGet(reader, "ed25519@22:legacy")) == "legacy-host-key", "migration adds missing trust"); wsClose(reader);
        check(!wook::migrateLegacySettings(importedData), "legacy migration runs only once");
        check(!wook::migrateLegacySettings(data + L"-absent"), "missing legacy folder is harmless");
        wchar_t restored[32768]{}; GetEnvironmentVariableW(L"WOOK_DATA_DIR", restored, 32768);
        check(restored == data, "migration restores the destination storage context");
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", importedData.c_str());
        check(wook::loadProfiles().size() == 2, "migration preserves the source folder");
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", (data + L"-same-root").c_str());
        wook::initializeDefaults();
        check(!wook::migrateLegacySettings(data + L"-same-root"), "source and destination cannot be the same folder");
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
        { std::ofstream f(std::filesystem::path(path), std::ios::binary); f << "WS1\nnot-hex=broken\n"; }
        check(wsOpen(path, 0) == nullptr, "malformed file fails closed");
        rejects([&] { wook::saveProfile(profile, profile.name); }, "corrupt record is not silently overwritten");
        auto failedMigration = data + L"-migration-failed";
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", failedMigration.c_str());
        rejects([&] { wook::migrateLegacySettings(data); }, "migration reports corrupt source data");
        GetEnvironmentVariableW(L"WOOK_DATA_DIR", restored, 32768);
        check(restored == failedMigration, "failed migration restores the destination context");
        check(!std::filesystem::exists(std::filesystem::path(failedMigration) / L"legacy-data-imported.txt"), "failed migration remains retryable");
        check(wook::loadProfiles().empty(), "corrupt source cannot partially migrate");
        free(path);
        check(wsPath(L"../outside", "x") == nullptr, "reject path traversal category");
        check(wsPath(L"sessions", std::string(101, 'x').c_str()) == nullptr, "bounded session filenames");
        testPasswords(data + L"-passwords");
        std::wcout << L"PASS: " << checks << L" checks. Isolated data: " << data << L"\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
}
