#include "sftp.hpp"
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <objbase.h>

namespace sftp {
namespace fs = std::filesystem;
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    HANDLE release() { auto out = value; value = nullptr; return out; }
};
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
std::vector<wchar_t> environment(HWND owner, const std::wstring &session, bool saved) {
    wchar_t *block = GetEnvironmentStringsW();
    require(block != nullptr, "Cannot read the process environment.");
    std::vector<std::wstring> values;
    for (auto p = block; *p; p += wcslen(p)+1) {
        if (_wcsnicmp(p, L"WOOK_PARENT_", 12) && _wcsnicmp(p, L"WOOK_PASSWORD_SESSION=", 22) && _wcsnicmp(p, L"WOOK_EDIT_SESSION=", 18)) values.emplace_back(p);
    }
    FreeEnvironmentStringsW(block);
    if (owner) {
        values.push_back(L"WOOK_PARENT_HWND=" + std::to_wstring((uintptr_t)owner));
        values.push_back(L"WOOK_PARENT_PID=" + std::to_wstring(GetCurrentProcessId()));
    }
    if (saved) values.push_back(L"WOOK_PASSWORD_SESSION=" + session);
    std::sort(values.begin(), values.end(), [](const auto &a, const auto &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> result;
    for (auto &value : values) { result.insert(result.end(), value.begin(), value.end()); result.push_back(0); }
    result.push_back(0); return result;
}
struct Transfer {
    HANDLE file; const std::function<void(uint64_t,uint64_t)> *update; std::atomic_bool *cancelled;
    static int read(void *p, void *data, int size) { DWORD count{}; return ReadFile(((Transfer *)p)->file, data, size, &count, nullptr) ? (int)count : -1; }
    static int write(void *p, const void *data, int size) { DWORD count{}; return WriteFile(((Transfer *)p)->file, data, size, &count, nullptr) ? (int)count : -1; }
    static int progress(void *p, uint64_t done, uint64_t size) { auto t = (Transfer *)p; (*t->update)(done,size); return !t->cancelled->load(); }
};
}
std::wstring join(const std::wstring &path, const std::wstring &name) { return path + (path.ends_with(L"/") ? L"" : L"/") + name; }
std::wstring uniqueName() { GUID id{}; require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create a unique transfer name."); wchar_t buffer[40]; StringFromGUID2(id, buffer, 40); return L".wshell-" + std::wstring(buffer) + L".part"; }
Client::Client(HWND owner, HANDLE job, const std::wstring &session, bool saved) {
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle inRead, inWrite, outRead, outWrite, error;
    require(CreatePipe(&inRead.value, &inWrite.value, &sa, 65536) && CreatePipe(&outRead.value, &outWrite.value, &sa, 65536), "Cannot create SFTP pipes.");
    require(SetHandleInformation(inWrite.value, HANDLE_FLAG_INHERIT, 0) && SetHandleInformation(outRead.value, HANDLE_FLAG_INHERIT, 0), "Cannot protect SFTP pipes.");
    error.value = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    require(error.value != INVALID_HANDLE_VALUE, "Cannot initialize SFTP diagnostics.");
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = inRead.value; si.StartupInfo.hStdOutput = outWrite.value; si.StartupInfo.hStdError = error.value;
    SIZE_T size{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<unsigned char> attributes(size); si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
    require(InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size), "Cannot initialize process isolation.");
    HANDLE handles[] = {inRead.value, outWrite.value, error.value};
    bool updated = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
    wchar_t self[32768]; GetModuleFileNameW(nullptr, self, 32768);
#ifdef WOOK_SFTP_TEST
    // Tests use the real, separately built release executable as the transport.
    GetEnvironmentVariableW(L"WOOK_SFTP_ENGINE", self, 32768);
#endif
    auto args = wook::quoteArg(self) + L" --terminal -load " + wook::quoteArg(session) + L" -wook-sftp";
    auto env = environment(owner, session, saved); PROCESS_INFORMATION pi{};
    bool started = updated && CreateProcessW(self, args.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                                             env.data(), nullptr, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    require(started, "Cannot start the internal SFTP connection.");
    Handle thread{pi.hThread}, child{pi.hProcess};
    if (job && !AssignProcessToJobObject(job, child.value)) { TerminateProcess(child.value, 1); throw std::runtime_error("Cannot supervise SFTP connection."); }
    codec = wsftp_create(this, read, write);
    if (!codec) { TerminateProcess(child.value, 1); throw std::runtime_error("Cannot allocate SFTP connection."); }
    if (ResumeThread(thread.value) == (DWORD)-1) { TerminateProcess(child.value, 1); wsftp_free(codec); codec = nullptr; throw std::runtime_error("Cannot resume the SFTP connection."); }
    process = child.release(); input = inWrite.release(); output = outRead.release();
}
Client::~Client() { cancel(); if (process) CloseHandle(process); if (input) CloseHandle(input); if (output) CloseHandle(output); wsftp_free(codec); }
void Client::cancel() { cancelled = true; std::lock_guard lock(processLock); if (process) TerminateProcess(process, 0); }
int Client::read(void *context, void *data, int size) {
    auto self = (Client *)context; ULONGLONG start = GetTickCount64();
    while (!self->cancelled) {
        DWORD available{}, count{};
        if (!PeekNamedPipe(self->output, nullptr, 0, nullptr, &available, nullptr)) return -1;
        if (available) return ReadFile(self->output, data, std::min<DWORD>(available, size), &count, nullptr) ? (int)count : -1;
        if (WaitForSingleObject(self->process, 0) == WAIT_OBJECT_0 || GetTickCount64()-start > 120000) { self->cancel(); return -1; }
        Sleep(5);
    }
    return -1;
}
int Client::write(void *context, const void *data, int size) {
    auto self = (Client *)context; DWORD count{};
    return !self->cancelled && WriteFile(self->input, data, size, &count, nullptr) ? (int)count : -1;
}
void Client::check(int ok) { if (!ok) throw std::runtime_error(wsftp_error(codec)); }
std::wstring Client::initialize() { check(wsftp_init(codec)); return canonical(L"."); }
std::wstring Client::canonical(const std::wstring &path) { char out[32769]; check(wsftp_realpath(codec, wook::utf8(path).c_str(), out, sizeof(out))); return wook::wide(out); }
std::vector<Entry> Client::list(const std::wstring &path) {
    struct Listing { std::vector<Entry> entries; bool failed{}; } result;
    check(wsftp_list(codec, wook::utf8(path).c_str(), [](void *p, const char *name, const WsFtpAttrs *a) -> int {
        auto &r = *(Listing *)p;
        try {
            auto text = wook::wide(name);
            if (text.empty() || std::any_of(text.begin(),text.end(),[](wchar_t c){return c < 32 || c == 127;})) return 0;
            r.entries.push_back({text,a->size,a->permissions,a->modified}); return 1;
        } catch (...) { r.failed = true; return 0; }
    }, &result));
    std::sort(result.entries.begin(),result.entries.end(),[](const auto &a,const auto &b){return a.directory() != b.directory() ? a.directory() : _wcsicmp(a.name.c_str(),b.name.c_str())<0;});
    return std::move(result.entries);
}
bool Client::exists(const std::wstring &path, WsFtpAttrs *out) {
    WsFtpAttrs a{}; if (wsftp_stat(codec, wook::utf8(path).c_str(), &a)) { if (out) *out = a; return true; }
    if (wsftp_status(codec) == 2) return false;
    check(0); return false;
}
void Client::mkdir(const std::wstring &path) { check(wsftp_mkdir(codec, wook::utf8(path).c_str())); }
void Client::rename(const std::wstring &from, const std::wstring &to) { require(!exists(to), "The destination already exists. Choose a different name."); check(wsftp_rename(codec, wook::utf8(from).c_str(), wook::utf8(to).c_str(), 0)); }
void Client::remove(const std::wstring &path, bool directory) { check(wsftp_remove(codec, wook::utf8(path).c_str(), directory)); }
void Client::upload(const std::wstring &local, const std::wstring &remote, bool replace, const std::function<void(uint64_t,uint64_t)> &progress) {
    Handle file{CreateFileW(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    require(file.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file.value, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)), "Select a regular local file; links and folders are not transferred.");
    WsFtpAttrs target{}; bool found = exists(remote, &target);
    require(!found || (replace && (target.permissions & 0170000) == 0100000), "The remote destination exists or is not a regular file. Refresh and confirm replacement.");
    require(!found || wsftp_atomic_replace(codec), "This server does not support atomic replacement. Choose a different filename.");
    auto temp = join(remote.substr(0, remote.find_last_of(L'/')), uniqueName());
    Transfer transfer{file.value, &progress, &cancelled};
    uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    check(wsftp_upload(codec, wook::utf8(temp).c_str(), size, Transfer::read, Transfer::progress, &transfer));
    check(wsftp_rename(codec, wook::utf8(temp).c_str(), wook::utf8(remote).c_str(), found));
}
void Client::download(const std::wstring &remote, const std::wstring &local, bool replace, const std::function<void(uint64_t,uint64_t)> &progress) {
    WsFtpAttrs a{}; require(exists(remote,&a) && (a.flags & 1) && (a.permissions & 0170000) == 0100000, "Select a regular remote file; links and folders are not transferred.");
    DWORD attributes = GetFileAttributesW(local.c_str());
    require(attributes == INVALID_FILE_ATTRIBUTES || (replace && !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))), "The local destination exists or is not a regular file. Refresh and confirm replacement.");
    auto temp = (fs::path(local).parent_path()/uniqueName()).wstring();
    try {
        Handle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        require(file.value != INVALID_HANDLE_VALUE, "Cannot create the local transfer file. Check folder permissions.");
        Transfer transfer{file.value, &progress, &cancelled};
        check(wsftp_download(codec, wook::utf8(remote).c_str(), a.size, Transfer::write, Transfer::progress, &transfer));
        require(FlushFileBuffers(file.value), "Cannot save the downloaded file. Check disk space.");
        CloseHandle(file.release());
        require(MoveFileExW(temp.c_str(), local.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)), "Cannot finish the download. The existing destination has been kept.");
    } catch (...) { DeleteFileW(temp.c_str()); throw; }
}
}
