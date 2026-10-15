static void testConnections(const std::wstring &data) {
    SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
    using namespace wook;
    Profile a; a.name = L"Alpha"; a.host = L"127.0.0.1"; a.user = L"fixture"; a.group = L"Legacy";
    Profile b = a; b.name = L"Beta";
    Profile c = a; c.name = L"Zulu"; c.group.clear();
    saveProfile(a, L"", PasswordAction::replace, L"test-only-organization-password"); saveProfile(b); saveProfile(c);
    auto path = wsPath(L"sessions", "Alpha");
    auto store = wsOpen(path, true);
    check(store && wsSet(store, "PortForwardings", "L9000=localhost:80") && wsSave(store), "Seed advanced connection settings");
    std::string encrypted = wsGet(store, passwordField); wsClose(store);
    auto folders = loadFolders(loadProfiles());
    check(folders.size() == 1 && folders[0].name == L"Legacy", "Existing group text becomes a folder without data migration");
    createFolder(L"빈 폴더 🚀"); createFolder(L"Staging");
    check(loadFolders(loadProfiles()).size() == 3, "Empty Unicode folders survive reload");
    rejects([&] { createFolder(L"staging"); }, "Folder names are unique ignoring case");
    for (auto name : {L"", L"  ", L"bad\nname", L" spaced ", L"bad\tname"})
        rejects([&] { createFolder(name); }, "Invalid folder names are rejected");
    rejects([&] { createFolder(std::wstring(101, L'x')); }, "Folder names have a UTF-8 byte limit");
    renameFolder(L"Legacy", L"Production");
    auto profiles = loadProfiles();
    check(profiles[0].group == L"Production" && profiles[1].group == L"Production", "Renaming a folder updates all its hosts");
    check(profiles[0].passwordSaved && profiles[0].host == a.host && profiles[0].user == a.user, "Folder changes preserve authentication identity");
    store = wsOpen(path, false);
    check(encrypted == wsGet(store, passwordField) && std::string(wsGet(store, "PortForwardings")) == "L9000=localhost:80",
          "Folder changes preserve exact encrypted credentials and advanced settings"); wsClose(store);
    collapseFolder(L"Production", true);
    check(loadFolders(profiles)[0].collapsed, "Folder collapse persists");
    moveFolder(L"Staging", -1); moveFolder(L"Staging", -1);
    check(loadFolders(profiles)[0].name == L"Staging", "Folder order persists");
    moveFolder(L"Staging", -1);
    check(loadFolders(profiles)[0].name == L"Staging", "Folder boundary movement is a no-op");
    moveProfileBy(L"Beta", -1);
    profiles = loadProfiles();
    check(profiles[0].name == L"Beta" && profiles[1].name == L"Alpha", "Host order persists within a folder");
    moveProfileBy(L"Beta", -1);
    check(loadProfiles()[0].name == L"Beta", "Host boundary movement does not wrap");
    moveProfile(L"Zulu", L"Production");
    profiles = loadProfiles();
    check(profiles[2].name == L"Zulu" && profiles[2].group == L"Production", "Moving to a folder appends to its hosts");
    auto production = loadFolders(profiles);
    check(!production[1].collapsed, "Moving a host expands its destination folder");
    auto edited = profiles[1]; edited.name = L"Renamed alpha";
    saveProfile(edited, L"Alpha");
    check(loadProfiles()[1].name == edited.name, "Renaming a host preserves its custom position");
    edited.alias = L"Same alias"; saveProfile(edited, edited.name);
    check(loadProfiles()[1].name == edited.name, "Ordinary host edits preserve custom order");
    edited.name = L"Another copy"; saveProfile(edited);
    check(loadProfiles().back().name == edited.name, "A newly saved copy does not inherit the original rank");
    // Lock a later member: no earlier member may have been committed yet.
    auto lockedPath = wsPath(L"sessions", "Zulu"); store = wsOpen(lockedPath, true);
    rejects([&] { renameFolder(L"Production", L"Rejected rename"); }, "Folder changes fail cleanly when a member is locked");
    wsClose(store);
    for (const auto &p : loadProfiles()) check(p.group == L"Production", "A lock failure leaves every member unchanged");
    check(SetFileAttributesW(lockedPath, FILE_ATTRIBUTE_READONLY) != 0, "Make a later member unwritable");
    rejects([&] { renameFolder(L"Production", L"Failed write"); }, "Folder changes report disk write failures");
    check(SetFileAttributesW(lockedPath, FILE_ATTRIBUTE_NORMAL) != 0, "Restore fixture permissions"); free(lockedPath);
    for (const auto &p : loadProfiles()) check(p.group == L"Production", "Earlier records roll back when a later write fails");
    check(loadFolders(loadProfiles())[1].name == L"Production", "Failed rename preserves folder metadata");
    rejects([&] { renameFolder(L"Production", L"Staging"); }, "Rename cannot merge folders accidentally");
    rejects([&] { moveProfile(L"Beta", L"Missing"); }, "Missing destination folders are rejected");
    rejects([&] { moveProfileBy(L"Missing", 1); }, "Missing host movements are rejected");
    moveProfile(L"Beta", L"");
    check(loadProfiles()[0].name == L"Renamed alpha", "Moving a host keeps other hosts in order");
    auto backup = data + L"\\organization.wshell";
    exportSettings(backup);
    auto before = loadProfiles(); auto foldersBefore = loadFolders(before);
    SetEnvironmentVariableW(L"WOOK_DATA_DIR", (data + L"-import").c_str());
    initializeDefaults(); importSettings(backup);
    profiles = loadProfiles(); folders = loadFolders(profiles);
    check(profiles.size() == before.size() && folders.size() == foldersBefore.size(), "Backups retain hosts and empty folders");
    for (size_t i = 0; i < profiles.size(); ++i)
        check(profiles[i].name == before[i].name && profiles[i].group == before[i].group && !profiles[i].passwordSaved,
              "Backup restores host order and membership without passwords");
    for (size_t i = 0; i < folders.size(); ++i)
        check(folders[i].name == foldersBefore[i].name && folders[i].collapsed == foldersBefore[i].collapsed,
              "Backup restores folder order and expansion state");
    SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
    deleteFolder(L"Production");
    profiles = loadProfiles();
    check(profiles.size() == before.size(), "Deleting a populated folder retains every host");
    for (const auto &p : profiles) check(p.group.empty(), "Deleted folder members move to the root");
    folders = loadFolders(profiles);
    check(folders.size() == 2, "Deleted folders do not reappear from stale group values");
    deleteFolder(L"빈 폴더 🚀");
    check(loadFolders(profiles).size() == 1, "Empty folders can be deleted");
    free(path);
}
