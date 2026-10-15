#include "connections.hpp"
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>

namespace wook {
namespace {
using Store = std::unique_ptr<WsStore, decltype(&wsClose)>;
Store open(const wchar_t *category, const std::wstring &name, bool write) {
    auto path = wsPath(category, utf8(name).c_str());
    Store result(wsOpen(path, write), wsClose); free(path);
    if (!result) throw std::runtime_error("Cannot read or lock connection organization. Check folder permissions and try again.");
    return result;
}
std::vector<Folder> readFolders(WsStore *store, const std::vector<Profile> &profiles) {
    std::vector<Folder> folders;
    std::set<std::wstring> names;
    auto countText = wsGet(store, "Count");
    size_t count = 0;
    if (countText) {
        std::string text(countText); size_t end = 0;
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid saved folder count.");
        count = std::stoul(text, &end);
        if (end != text.size() || count > 3000) throw std::runtime_error("Invalid saved folder count.");
    }
    for (size_t i = 0; i < count; ++i) {
        auto name = wsGet(store, ("Name" + std::to_string(i)).c_str());
        auto collapsed = wsGet(store, ("Collapsed" + std::to_string(i)).c_str());
        if (!name || !*name || !names.insert(wide(name)).second)
            throw std::runtime_error("Invalid or duplicate saved folder.");
        folders.push_back({wide(name), collapsed && std::string_view(collapsed) == "1"});
    }
    std::set<std::wstring> legacy;
    for (const auto &p : profiles) if (!p.group.empty() && !names.contains(p.group)) legacy.insert(p.group);
    for (const auto &name : legacy) folders.push_back({name});
    return folders;
}
size_t folderIndex(const std::vector<Folder> &folders, const std::wstring &name) {
    for (size_t i = 0; i < folders.size(); ++i) if (folders[i].name == name) return i;
    throw std::runtime_error("This folder no longer exists. Refresh the connection list and try again.");
}
void uniqueName(const std::vector<Folder> &folders, const std::wstring &name, const std::wstring &original = L"") {
    validateFolderName(name);
    for (const auto &f : folders) if (f.name != original && !_wcsicmp(f.name.c_str(), name.c_str()))
        throw std::runtime_error("A folder already has this name.");
}
void set(WsStore *store, const std::string &key, const std::wstring &value) {
    if (!wsSet(store, key.c_str(), utf8(value).c_str())) throw std::runtime_error("Cannot prepare connection organization for saving.");
}
// Keep every writer locked until the whole change commits. A failed write rolls
// back earlier records, including untouched advanced settings and credentials.
struct Change {
    Store current, previous;
    explicit Change(Store store) : current(std::move(store)), previous(wsOpen(current->path, false), wsClose) {
        if (!previous) throw std::runtime_error("Cannot read connection settings before saving.");
    }
};
struct Organization {
    Change layout{open(L"workspace", L"folders", true)};
    std::vector<Profile> profiles = loadProfiles();
    std::vector<Folder> folders = readFolders(layout.current.get(), profiles);
    std::vector<Change> changes;
    WsStore *session(const std::wstring &name) {
        changes.emplace_back(open(L"sessions", name, true));
        if (!changes.back().current->exists) throw std::runtime_error("The saved host no longer exists.");
        return changes.back().current.get();
    }
    void save() {
        if (folders.size() > 3000) throw std::runtime_error("At most 3000 folders can be saved.");
        set(layout.current.get(), "Count", std::to_wstring(folders.size()));
        for (size_t i = 0; i < folders.size(); ++i) {
            set(layout.current.get(), "Name" + std::to_string(i), folders[i].name);
            set(layout.current.get(), "Collapsed" + std::to_string(i), folders[i].collapsed ? L"1" : L"0");
        }
        changes.push_back(std::move(layout));
        size_t saved = 0;
        for (; saved < changes.size(); ++saved) if (!wsSave(changes[saved].current.get())) break;
        if (saved == changes.size()) return;
        bool restored = true;
        while (saved) {
            auto &change = changes[--saved];
            if (change.previous->exists) {
                std::swap(change.current->pairs, change.previous->pairs);
                restored = wsSave(change.current.get()) && restored;
            }
            else restored = (DeleteFileW(change.current->path) || GetLastError() == ERROR_FILE_NOT_FOUND) && restored;
        }
        throw std::runtime_error(restored ? "Could not save connection organization. The change was undone. Check disk space and permissions." :
            "Could not fully restore connection organization after a save failure. Reload the list and check your settings backup.");
    }
    void saveOrder(const std::wstring &moved = L"") {
        for (size_t i = 0; i < profiles.size(); ++i) {
            const auto &p = profiles[i];
            if (p.order == i && p.name != moved) continue;
            auto store = session(p.name);
            set(store, "WookOrder", std::to_wstring(i));
            if (p.name == moved) set(store, "WookGroup", p.group);
        }
        save();
    }
};
}
void validateFolderName(const std::wstring &name) {
    if (trim(name).empty() || trim(name) != name || utf8(name).size() > 100 ||
        std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Choose a folder name of 1-100 UTF-8 bytes on one line, without surrounding spaces.");
}
std::vector<Folder> loadFolders(const std::vector<Profile> &profiles) {
    auto store = open(L"workspace", L"folders", false);
    return readFolders(store.get(), profiles);
}
void createFolder(const std::wstring &name) {
    Organization edit; uniqueName(edit.folders, name);
    edit.folders.push_back({name}); edit.save();
}
void renameFolder(const std::wstring &name, const std::wstring &replacement) {
    Organization edit; auto index = folderIndex(edit.folders, name);
    uniqueName(edit.folders, replacement, name);
    if (name == replacement) return;
    for (const auto &p : edit.profiles) if (p.group == name) set(edit.session(p.name), "WookGroup", replacement);
    edit.folders[index].name = replacement; edit.save();
}
void deleteFolder(const std::wstring &name) {
    Organization edit; auto index = folderIndex(edit.folders, name);
    for (const auto &p : edit.profiles) if (p.group == name) set(edit.session(p.name), "WookGroup", L"");
    edit.folders.erase(edit.folders.begin() + index); edit.save();
}
void collapseFolder(const std::wstring &name, bool collapsed) {
    Organization edit;
    edit.folders[folderIndex(edit.folders, name)].collapsed = collapsed; edit.save();
}
void moveFolder(const std::wstring &name, int direction) {
    Organization edit; auto index = folderIndex(edit.folders, name);
    if (direction != -1 && direction != 1) throw std::runtime_error("Invalid folder movement.");
    auto next = (int)index + direction;
    if (next < 0 || next >= (int)edit.folders.size()) return;
    std::swap(edit.folders[index], edit.folders[next]); edit.save();
}
void moveProfile(const std::wstring &name, const std::wstring &folder) {
    Organization edit;
    if (!folder.empty()) edit.folders[folderIndex(edit.folders, folder)].collapsed = false;
    auto from = std::find_if(edit.profiles.begin(), edit.profiles.end(), [&](const auto &p) { return p.name == name; });
    if (from == edit.profiles.end()) throw std::runtime_error("The saved host no longer exists.");
    if (from->group == folder) return;
    auto moved = *from; moved.group = folder; edit.profiles.erase(from);
    auto position = edit.profiles.end();
    for (auto it = edit.profiles.begin(); it != edit.profiles.end(); ++it) if (it->group == folder) position = it + 1;
    edit.profiles.insert(position, std::move(moved)); edit.saveOrder(name);
}
void moveProfileBy(const std::wstring &name, int direction) {
    Organization edit;
    if (direction != -1 && direction != 1) throw std::runtime_error("Invalid host movement.");
    auto from = std::find_if(edit.profiles.begin(), edit.profiles.end(), [&](const auto &p) { return p.name == name; });
    if (from == edit.profiles.end()) throw std::runtime_error("The saved host no longer exists.");
    for (int i = (int)(from - edit.profiles.begin()) + direction; i >= 0 && i < (int)edit.profiles.size(); i += direction) {
        if (edit.profiles[i].group != from->group) continue;
        std::iter_swap(from, edit.profiles.begin() + i); edit.saveOrder(); return;
    }
}
}
