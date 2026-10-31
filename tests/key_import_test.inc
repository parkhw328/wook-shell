int runKeyImportTest() {
    int checks = 0;
    auto check = [&](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); ++checks; };
    auto directory = std::filesystem::path(wook::executableDirectory());
    std::ofstream report(directory / L"key-import-result.json");
    try {
        using Key = std::unique_ptr<WsKey, decltype(&wsKeyFree)>;
        const wchar_t *files[] = {L"oracle-ssh-key.key", L"encrypted-rsa.pem", L"encrypted-ed25519.openssh"};
        const wchar_t *publicFiles[] = {L"plain/identity.pub", L"rsa/identity.pub", L"ed/id_ed25519.PUB"};
        auto rejects = [&](const std::filesystem::path &path, const char *passphrase) {
            auto count = registeredPrivateKeys().size(); bool rejected = false;
            try { preparePrivateKey(nullptr, path.wstring(), passphrase); } catch (const std::exception &) { rejected = true; }
            check(rejected && registeredPrivateKeys().size() == count, "Rejected public keys must not create a private key entry");
        };
        rejects(directory / publicFiles[1], "incorrect");
        bool cancelledPrompt = false;
        std::jthread cancel([&] {
            for (int i = 0; i < 250; ++i) {
                if (auto prompt = FindWindowW(L"wShell.TextPrompt", L"Import private key")) {
                    DWORD pid = 0; GetWindowThreadProcessId(prompt, &pid);
                    if (pid == GetCurrentProcessId()) {
                        cancelledPrompt = true; PostMessageW(prompt, WM_COMMAND, IDCANCEL, 0); return;
                    }
                }
                Sleep(20);
            }
        });
        auto cancelled = preparePrivateKey(nullptr, (directory / publicFiles[1]).wstring());
        cancel.join();
        check(cancelledPrompt && cancelled.empty() && registeredPrivateKeys().empty(), "Cancelling a matching key passphrase must not register or select a key");

        // A PPK in the selected folder is reused without decrypting or copying.
        auto sibling = directory / L"sibling"; std::filesystem::create_directories(sibling);
        Key local(wsKeyGenerate(0), wsKeyFree); check(bool(local), "Sibling key fixture must generate");
        check(wsKeySave(local.get(), (sibling / L"unrelated-name.ppk").c_str(), "fixture-passphrase"), "Sibling encrypted PPK must save");
        auto publicText = wsKeyPublic(local.get());
        { std::ofstream out(sibling / L"identity.pub"); out << publicText << '\n'; }
        wsKeyStringFree(publicText);
        check(preparePrivateKey(nullptr, (sibling / L"identity.pub").wstring()) == (sibling / L"unrelated-name.ppk").wstring(), "Sibling encrypted PPK must match by content without a prompt");
        check(registeredPrivateKeys().empty(), "Selecting a sibling PPK must not duplicate it");
        std::vector<std::wstring> imported;
        for (int i = 0; i < 3; ++i) {
            auto source = directory / files[i]; int encrypted = 0;
            check(wsKeyInspect(source.c_str(), &encrypted) == WS_KEY_IMPORT, "Private key format must be detected from its contents");
            check(encrypted == (i != 0), "Encrypted source keys must be identified");
            if (encrypted) { Key wrong(wsKeyLoad(source.c_str(), "incorrect"), wsKeyFree); check(!wrong, "Wrong key passphrases must fail"); }
            const char *passphrase = encrypted ? "fixture-passphrase" : "";
            Key key(wsKeyLoad(source.c_str(), passphrase), wsKeyFree);
            check(bool(key), "Independent PEM/OpenSSH key fixture must load");
            auto converted = preparePrivateKey(nullptr, (directory / publicFiles[i]).wstring(), passphrase);
            imported.push_back(converted);
            check(wsKeyInspect(converted.c_str(), &encrypted) == WS_KEY_PPK && encrypted == (i != 0), "Registered PPK must retain encryption");
            Key restored(wsKeyLoad(converted.c_str(), passphrase), wsKeyFree);
            check(bool(restored), "Registered key must reopen");
            auto expected = wsKeyPublic(key.get()), actual = wsKeyPublic(restored.get());
            bool same = expected && actual && std::string(expected) == actual;
            wsKeyStringFree(expected); wsKeyStringFree(actual);
            check(same, "Key registration must preserve public identity");
            check(preparePrivateKey(nullptr, converted) == converted, "A registered key must be reused without another import");
            check(preparePrivateKey(nullptr, (directory / publicFiles[i]).wstring()) == converted, "Public selection must reuse an encrypted registered key without prompting or duplicating it");
            std::ofstream pathFile(directory / (L"converted-" + std::to_wstring(i) + L".path")); pathFile << wook::utf8(converted);
        }
        auto before = registeredPrivateKeys().size();
        check(before == 3, "All imported keys must be listed for reuse");
        for (auto filename : {L"missing/identity.pub", L"mismatch/identity.pub", L"malformed.key", L"malformed.pub", L"algorithm-mismatch.pub"})
            rejects(directory / filename, "fixture-passphrase");
        auto publicPath = directory / L"public-only.key";
        check(wsKeyInspect(publicPath.c_str(), nullptr) == WS_KEY_PUBLIC, "Public-only detection must not depend on a .pub extension");
        check(preparePrivateKey(nullptr, publicPath.wstring()) == imported[0], "Comments and public file extensions must not affect identity matching");
        check(preparePrivateKey(nullptr, (directory / L"rfc4716.key").wstring()) == imported[2], "RFC4716 public input must reuse the matching registered private key");
        check(registeredPrivateKeys().size() == before, "Public lookups must preserve the library");
        Key publicOnly(wsKeyLoad(publicPath.c_str(), ""), wsKeyFree);
        check(!publicOnly, "Public-only material must never load as a private key");
        auto direct = preparePrivateKey(nullptr, (directory / files[0]).wstring());
        check(wsKeyInspect(direct.c_str(), nullptr) == WS_KEY_PPK && registeredPrivateKeys().size() == before + 1,
              "Direct private-key import must still register a usable PPK");
        Profile profile; profile.name = L"Oracle format integration"; profile.host = L"127.0.0.1"; profile.user = L"oracle";
        wchar_t port[32]{}, pin[200]{};
        GetEnvironmentVariableW(L"WOOK_TEST_PORT", port, 32); profile.port = _wtoi(port);
        GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT", pin, 200);
        profile.keyFile = publicPath.wstring(); wook::saveProfile(profile);
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
