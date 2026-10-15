#pragma once
#include "core.hpp"

namespace wook {
struct Folder {
    std::wstring name;
    bool collapsed = false;
};
// Legacy WookGroup values are included even before a folder has been saved.
std::vector<Folder> loadFolders(const std::vector<Profile> &profiles);
void validateFolderName(const std::wstring &name);
void createFolder(const std::wstring &name);
void renameFolder(const std::wstring &name, const std::wstring &replacement);
void deleteFolder(const std::wstring &name);
void collapseFolder(const std::wstring &name, bool collapsed);
void moveFolder(const std::wstring &name, int direction);
void moveProfile(const std::wstring &name, const std::wstring &folder);
void moveProfileBy(const std::wstring &name, int direction);
}
