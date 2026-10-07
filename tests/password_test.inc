static void testPasswords(const std::wstring &data) {
    SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
    wook::Profile p; p.name = L"Password host"; p.host = L"server.example.invalid"; p.user = L"deploy";
    const std::wstring secret = L"  test-only 암호 !  ";
    auto open = [&]() {
        wchar_t *path = wsPath(L"sessions", wook::utf8(p.name).c_str());
        auto raw = wsOpen(path, 1); free(path);
        if (!raw) throw std::runtime_error("Cannot open password test record");
        return std::unique_ptr<WsStore, decltype(&wsClose)>(raw, wsClose);
    };
    auto read = [&](const char *host, int port, const char *user, char **password) {
        return wsLoadSavedPassword(wook::utf8(p.name).c_str(), host, port, user, password);
    };
    wook::saveProfile(p, L"", wook::PasswordAction::replace, secret);
    check(wook::loadProfiles()[0].passwordSaved, "saved password status");
    char *password = nullptr;
    check(read("SERVER.EXAMPLE.INVALID", 22, "deploy", &password) == 1, "DPAPI decrypt for the current Windows user");
    check(std::string(password) == wook::utf8(secret), "Unicode and whitespace in passwords are preserved"); wsPasswordFree(password);
    check(read("other.example.invalid", 22, "deploy", &password) == 0 && !password, "password is bound to the server");
    check(read("server.example.invalid", 2222, "deploy", &password) == 0 && !password, "password is bound to the port");
    check(read("server.example.invalid", 22, "other", &password) == 0 && !password, "password is bound to the username");
    std::string ciphertext;
    { auto store = open(); ciphertext = wsGet(store.get(), wook::passwordField);
      check(!ciphertext.empty() && ciphertext.find("test-only") == std::string::npos, "record contains ciphertext only"); }
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret);
    { auto store = open(); check(ciphertext != wsGet(store.get(), wook::passwordField), "DPAPI uses fresh protection data"); }
    auto backup = data + L"\\password-excluded.wshell";
    wook::exportSettings(backup);
    { std::ifstream file(std::filesystem::path(backup), std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(file)), {});
      check(bytes.find(wook::passwordField) == std::string::npos && bytes.find(wook::passwordScopeField) == std::string::npos,
            "settings export omits encrypted credentials and their bindings"); }
    // Even manually constructed backups cannot install encrypted credential fields.
    std::string forged = "WSB1";
    auto number = [&](size_t n) { for (int i = 0; i < 4; ++i) forged += char((n >> (i * 8)) & 255); };
    auto field = [&](const std::string &value) { number(value.size()); forged += value; };
    number(1); field("sessions"); field("Imported credential"); number(5);
    field("HostName"); field("server.example.invalid"); field("UserName"); field("deploy");
    field("Protocol"); field("ssh"); field(wook::passwordField); field(ciphertext);
    field(wook::passwordScopeField); field("untrusted binding");
    auto forgedPath = data + L"\\forged.wshell";
    { std::ofstream file(std::filesystem::path(forgedPath), std::ios::binary); file << forged; }
    wook::importSettings(forgedPath);
    { wchar_t *path = wsPath(L"sessions", "Imported credential"); auto store = wsOpen(path, 0); free(path);
      check(store && !wsGet(store, wook::passwordField) && !wsGet(store, wook::passwordScopeField), "import rejects credential injection"); wsClose(store); }
    p.group = L"Renamed group"; wook::saveProfile(p, p.name);
    check(read("server.example.invalid", 22, "deploy", &password) == 1, "ordinary edits keep the saved password"); wsPasswordFree(password);
    auto oldName = p.name; p.name = L"Renamed password host"; wook::saveProfile(p, oldName);
    check(read("server.example.invalid", 22, "deploy", &password) == 1, "host rename retains the saved password"); wsPasswordFree(password);
    { auto store = open(); std::string damaged = wsGet(store.get(), wook::passwordField);
      damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
      wsSet(store.get(), wook::passwordField, damaged.c_str()); check(wsSave(store.get()), "save corrupted ciphertext fixture"); }
    check(read("server.example.invalid", 22, "deploy", &password) == -1 && !password, "tampered password fails without returning plaintext");
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret);
    p.host = L"changed.example.invalid"; wook::saveProfile(p, p.name);
    check(read("server.example.invalid", 22, "deploy", &password) == 0, "changing the destination removes the saved password");
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret);
    wook::saveProfile(p, p.name, wook::PasswordAction::forget);
    check(read("changed.example.invalid", 22, "deploy", &password) == 0, "forget removes stored credentials");
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret);
    p.keyFile = L"C:\\fixture-key.ppk"; wook::saveProfile(p, p.name);
    check(read("changed.example.invalid", 22, "deploy", &password) == 0, "switching to key authentication removes saved credentials");
    p.keyFile.clear(); p.user.clear();
    rejects([&] { wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret); }, "saved password requires an explicit username");
    p.user = L"deploy";
    rejects([&] { wook::saveProfile(p, p.name, wook::PasswordAction::replace, std::wstring(1025, L'x')); }, "password length is bounded");
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, secret); wook::deleteProfile(p.name);
    check(read("changed.example.invalid", 22, "deploy", &password) == 0, "deleting a host deletes its encrypted password");
}
