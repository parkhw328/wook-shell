static void testLaunch(const std::wstring &data) {
    SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
    auto parse = [](std::initializer_list<std::wstring_view> args) { return wook::parseLaunchArguments({args.begin(), args.size()}); };
    check(parse({}).mode == wook::LaunchRequest::Mode::workspace, "Empty launch opens workspace");
    check(parse({L"--help"}).mode == wook::LaunchRequest::Mode::help, "Help is read-only");
    check(parse({L"--preview"}).mode == wook::LaunchRequest::Mode::preview, "Preview remains supported");
    auto launch = parse({L"-ssh", L"deploy@Example.invalid", L"-P", L"2222", L"--name", L"서울 launch", L"-pw", L"한글 password & | \\\""});
    check(launch.profile.user == L"deploy" && launch.profile.host == L"Example.invalid" && launch.profile.port == 2222, "PuTTY launch options");
    check(std::wstring_view(launch.password.data(), launch.password.size()) == L"한글 password & | \\\"", "Password is preserved literally");
    auto p = wook::resolveLaunchProfile(launch);
    check(wook::loadProfiles().size() == 1 && p.name == L"서울 launch" && !wook::loadProfiles()[0].passwordSaved, "New host saved without persisting launch password");
    p.alias = L"Preserved alias"; p.fontSize = 16;
    wook::saveProfile(p, p.name, wook::PasswordAction::replace, L"previous saved password");
    auto repeat = parse({L"--host", L"example.invalid", L"--user", L"deploy", L"--port", L"2222"});
    auto reused = wook::resolveLaunchProfile(repeat);
    check(reused.name == p.name && reused.alias == p.alias && reused.fontSize == 16 && reused.passwordSaved && wook::loadProfiles().size() == 1,
          "Same endpoint reuses host and preserves customized settings and saved password");
    char *secret = nullptr;
    {
        wook::LaunchPassword once(p, {launch.password.data(), launch.password.size()});
        auto key = wook::utf8(once.name());
        check(wsLoadSavedPassword(key.c_str(), "example.invalid", 2222, "other-user", &secret) == 0, "Launch password cannot cross accounts");
        check(wsLoadSavedPassword(key.c_str(), "other.invalid", 2222, "deploy", &secret) == 0, "Launch password cannot cross hosts");
        check(wsLoadSavedPassword(key.c_str(), "example.invalid", 22, "deploy", &secret) == 0, "Launch password cannot cross ports");
        check(wsLoadSavedPassword(key.c_str(), "example.invalid", 2222, "deploy", &secret) == 1 &&
              std::string(secret) == wook::utf8(std::wstring(launch.password.data(), launch.password.size())), "DPAPI one-use password decrypts for its exact endpoint");
        wsPasswordFree(secret); secret = nullptr;
        check(wsLoadSavedPassword(key.c_str(), "example.invalid", 2222, "deploy", &secret) == 0, "One-use password is removed before it can be reused");
    }
    check(wsLoadSavedPassword(wook::utf8(p.name).c_str(), "example.invalid", 2222, "deploy", &secret) == 1 && std::string(secret) == "previous saved password",
          "One-use authentication leaves the saved password unchanged");
    wsPasswordFree(secret);
    std::wstring temporary;
    { wook::LaunchPassword unused(p, L"never consumed"); temporary = unused.name(); }
    auto path = wsPath(L"sessions", wook::utf8(temporary).c_str());
    check(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES, "Unused password record is removed on teardown"); free(path);
    auto collision = parse({L"other.invalid", L"--name", L"서울 launch"});
    rejects([&] { wook::resolveLaunchProfile(collision); }, "Name collision cannot overwrite another destination");
    check(wook::loadProfiles().size() == 1, "Collision leaves saved hosts unchanged");
    auto otherAccount = parse({L"other@example.invalid", L"-P", L"2222"});
    wook::resolveLaunchProfile(otherAccount);
    check(wook::loadProfiles().size() == 2, "Different account creates a separate saved host");
    auto ipv6 = parse({L"ssh://user@[::1]:2200"});
    check(ipv6.profile.host == L"::1" && ipv6.profile.port == 2200, "IPv6 launch parsing");
    check(parse({L"host.invalid", L"-l", L"user", L"--password-stdin"}).passwordStdin, "Password pipe opt-in");
    for (auto args : {std::vector<std::wstring_view>{L"-ssh"}, {L"host.invalid", L"-P", L"0"}, {L"host.invalid", L"-P", L"65536"},
                     {L"--host", L"host.invalid", L"other.invalid"}, {L"host.invalid", L"--user"}, {L"host.invalid", L"--unknown"},
                     {L"user@host.invalid", L"-l", L"other"}, {L"host.invalid", L"-pw", L"password"}, {L"telnet://host.invalid"},
                     {L"user@host.invalid", L"-pw", L"password", L"--password-stdin"}, {L"host.invalid", L"-P", L"22oops"}})
        rejects([&] { wook::parseLaunchArguments(args); }, "Invalid launch arguments are rejected");
    auto quoted = wook::parseLaunchCommand(L"\"C:\\a b\\wShell.exe\" -ssh user@host.invalid --name \"이름 with spaces\" -pw \"literal & | value\"");
    check(quoted.profile.name == L"이름 with spaces" && std::wstring_view(quoted.password.data(), quoted.password.size()) == L"literal & | value", "Windows quoting reaches the parser intact");
    launch.clearPassword(); check(launch.password.empty(), "Password buffer cleared after launch");
}
