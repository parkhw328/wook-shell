#pragma once
#include <windows.h>
#include <oleidl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fileDrag {
struct File { std::wstring name; uint64_t size; };
using Read = std::function<HRESULT(size_t, IStream **)>;
IDataObject *local(const std::vector<std::wstring> &paths);
IDataObject *remote(const std::vector<File> &files, Read read);
HRESULT run(IDataObject *data);
#ifdef WOOK_UI_TEST
extern std::function<HRESULT(IDataObject *)> probe;
#endif
bool hasPaths(IDataObject *data);
std::vector<std::wstring> paths(IDataObject *data);
using Accept = std::function<bool(IDataObject *)>;
using Drop = std::function<bool(IDataObject *)>;
IDropTarget *target(HWND window, Accept accept, Drop drop);
HRESULT openStream(const std::wstring &path, std::shared_ptr<void> lifetime, IStream **stream);
}
