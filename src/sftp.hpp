#pragma once
#include "core.hpp"
#include "wsftp.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace sftp {
struct Entry {
    std::wstring name;
    uint64_t size{};
    uint32_t mode{}, modified{};
    bool directory() const { return (mode & 0170000) == 0040000; }
    bool regular() const { return (mode & 0170000) == 0100000; }
};
std::wstring join(const std::wstring &path, const std::wstring &name);
std::wstring uniqueName();
class Client {
    HANDLE process{}, input{}, output{};
    std::mutex processLock;
    std::atomic_bool cancelled{};
    WsFtp *codec{};
    static int read(void *, void *, int);
    static int write(void *, const void *, int);
    void check(int);
public:
    // Called on the GUI thread so environment snapshots cannot race GUI launches.
    Client(HWND owner, HANDLE job, const std::wstring &session, bool saved);
    ~Client();
    Client(const Client &) = delete;
    void cancel();
    std::wstring initialize();
    std::wstring canonical(const std::wstring &path);
    std::vector<Entry> list(const std::wstring &path);
    bool exists(const std::wstring &path, WsFtpAttrs *attrs = nullptr);
    void mkdir(const std::wstring &path);
    void rename(const std::wstring &from, const std::wstring &to);
    void remove(const std::wstring &path, bool directory);
    void upload(const std::wstring &local, const std::wstring &remote, bool replace,
                const std::function<void(uint64_t,uint64_t)> &progress);
    void download(const std::wstring &remote, const std::wstring &local, bool replace,
                  const std::function<void(uint64_t,uint64_t)> &progress);
};
}
