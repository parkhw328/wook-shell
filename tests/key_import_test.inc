int runKeyImportTest() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); ++checks; };
    auto directory = std::filesystem::path(wook::executableDirectory());
    std::ofstream report(directory / L"key-import-result.json");
    try {
        using Key = std::unique_ptr<WsKey, decltype(&wsKeyFree)>;
        const wchar_t *files[] = {L"oracle-ssh-key.key", L"encrypted-rsa.pem", L"encrypted-ed25519.openssh"};
        std::vector<std::wstring> imported;
        for (int i = 0; i < 3; ++i) {
            auto source = directory / files[i]; int encrypted = 0;
            check(wsKeyInspect(source.c_str(), &encrypted) == WS_KEY_IMPORT, "Private key format must be detected from its contents");
            check(encrypted == (i != 0), "Encrypted source keys must be identified");
            if (encrypted) { Key wrong(wsKeyLoad(source.c_str(), "incorrect"), wsKeyFree); check(!wrong, "Wrong key passphrases must fail"); }
            const char *passphrase = encrypted ? "fixture-passphrase" : "";
            Key key(wsKeyLoad(source.c_str(), passphrase), wsKeyFree);
            check(bool(key), "Independent PEM/OpenSSH key fixture must load");
            auto converted = i ? registerPrivateKey(key.get(), source.wstring(), passphrase) : preparePrivateKey(nullptr, source.wstring());
            imported.push_back(converted);
            check(wsKeyInspect(converted.c_str(), &encrypted) == WS_KEY_PPK && encrypted == (i != 0), "Registered PPK must retain encryption");
            Key restored(wsKeyLoad(converted.c_str(), passphrase), wsKeyFree);
            check(bool(restored), "Registered key must reopen");
            auto expected = wsKeyPublic(key.get()), actual = wsKeyPublic(restored.get());
            bool same = expected && actual && std::string(expected) == actual;
            wsKeyStringFree(expected); wsKeyStringFree(actual);
            check(same, "Key registration must preserve public identity");
            check(preparePrivateKey(nullptr, converted) == converted, "A registered key must be reused without another import");
            std::ofstream pathFile(directory / (L"converted-" + std::to_wstring(i) + L".path")); pathFile << wook::utf8(converted);
        }
        auto before = registeredPrivateKeys().size();
        check(before == 3, "All imported keys must be listed for reuse");
        for (auto filename : {L"public-only.key", L"malformed.key"}) {
            bool rejected = false;
            try { preparePrivateKey(nullptr, (directory / filename).wstring()); } catch (const std::exception &) { rejected = true; }
            check(rejected && registeredPrivateKeys().size() == before, "Invalid or public-only input must not create a key entry");
        }
        auto publicPath = directory / L"public-only.key";
        check(wsKeyInspect(publicPath.c_str(), nullptr) == WS_KEY_PUBLIC, "Public-only detection must not depend on a .pub extension");
        check(std::string(wsKeyError()).find("only a public key") != std::string::npos, "Public-only files need an actionable error");
        Profile profile; profile.name = L"Oracle format integration"; profile.host = L"127.0.0.1"; profile.user = L"oracle";
        wchar_t port[32]{}, pin[200]{};
        GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32); profile.port = _wtoi(port);
        GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", pin, 200);
        profile.keyFile = (directory / files[0]).wstring(); wook::saveProfile(profile);
        auto path = wsPath(L"sessions", wook::utf8(profile.name).c_str()); auto store = wsOpen(path, true); free(path);
        check(store && wsSet(store, "SSHManualHostKeys", wook::utf8(pin).c_str()) && wsSave(store), "Key login fixture must pin its loopback server"); wsClose(store);
        wook::exportSettings((directory / L"key-backup.wshell").wstring());
        wchar_t supplied[32768]{}; bool inspected = GetEnvironmentVariableW(L"WOOK_TEST_INSPECT_KEY", supplied, 32768) != 0;
        if (inspected) { Key key(wsKeyLoad(supplied, ""), wsKeyFree); check(bool(key), "Supplied unencrypted private key must be readable"); }
        report << "{\"passed\":true,\"checks\":" << checks << ",\"suppliedKeyReadable\":" << (inspected ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception &) {
        report << "{\"passed\":false,\"checks\":" << checks << "}\n"; return 1;
    }
}
