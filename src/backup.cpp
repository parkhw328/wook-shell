#include "backup.hpp"
#include "core.hpp"
#include "credentials.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace wook {
namespace {
constexpr size_t maxBackup = 32 * 1024 * 1024;
struct Record { std::string category, name; std::map<std::string, std::string> pairs; };
using Store = std::unique_ptr<WsStore, decltype(&wsClose)>;
Store open(const Record &r, bool write) {
    wchar_t *path = wsPath(wide(r.category).c_str(), r.name.c_str());
    auto result = wsOpen(path, write); free(path);
    if (!result) throw std::runtime_error("Cannot open a settings record. Check permissions or another running wShell.");
    return Store(result, wsClose);
}
void number(std::string &out, size_t n) { for (int i = 0; i < 4; ++i) out += char((n >> (8 * i)) & 255); }
void field(std::string &out, const std::string &s) {
    if (s.size() > maxBackup || out.size() + s.size() + 4 > maxBackup) throw std::runtime_error("Settings backup exceeds 32 MiB.");
    number(out, s.size()); out += s;
}
struct Reader {
    const std::string &data; size_t at = 4;
    size_t number() {
        if (at + 4 > data.size()) throw std::runtime_error("Truncated settings backup.");
        size_t n = 0; for (int i = 0; i < 4; ++i) n |= size_t((unsigned char)data[at++]) << (i * 8); return n;
    }
    std::string field(size_t limit) {
        size_t n = number(); if (n > limit || n > data.size() - at) throw std::runtime_error("Invalid settings backup field.");
        auto s = data.substr(at, n); at += n;
        if (s.find('\0') != std::string::npos) throw std::runtime_error("NUL characters are not allowed in settings.");
        wide(s); return s;
    }
};
bool allowedName(const Record &r) {
    return (r.category == "sessions" || r.category == "trust" || r.category == "cas") &&
        !r.name.empty() && r.name.size() <= 100 && !r.name.starts_with("__wook_") &&
        (r.category != "trust" || r.name == "hostkeys");
}
}
std::wstring exportSettings(const std::wstring &path) {
    std::vector<Record> records;
    for (const wchar_t *category : {L"sessions", L"trust", L"cas"}) {
        size_t count = 0; auto names = wsList(category, &count);
        try {
            for (size_t i = 0; i < count; ++i) {
                Record record{utf8(category), names[i], {}}; if (!allowedName(record)) continue;
                auto store = open(record, false);
                for (auto pair = store->pairs; pair; pair = pair->next)
                    if (!isCredentialField(pair->key)) record.pairs.emplace(pair->key, pair->value);
                records.push_back(std::move(record));
            }
        } catch (...) { wsFreeList(names, count); throw; }
        wsFreeList(names, count);
    }
    if (records.size() > 10000) throw std::runtime_error("Too many records to export.");
    std::string out = "WSB1"; number(out, records.size());
    for (const auto &r : records) {
        field(out, r.category); field(out, r.name); number(out, r.pairs.size());
        for (const auto &[key, value] : r.pairs) { field(out, key); field(out, value); }
    }
    std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create the backup file.");
    DWORD written = 0;
    bool ok = WriteFile(file, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileW(temp.c_str()); throw std::runtime_error("Cannot finish writing the backup file."); }
    return L"Settings exported.\n\nIncludes sessions, trusted host keys and host authorities. Private-key files are not included.\n\n"
        L"Passwords are not exported or imported, even in encrypted form. Enter them again for imported hosts.";
}
std::wstring importSettings(const std::wstring &path) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    auto size = input.tellg();
    if (!input || size < 8 || size > (std::streamoff)maxBackup) throw std::runtime_error("Select a valid wShell settings backup (up to 32 MiB).");
    std::string bytes((size_t)size, '\0'); input.seekg(0); input.read(bytes.data(), size);
    if (!input || bytes.substr(0, 4) != "WSB1") throw std::runtime_error("This is not a wShell settings backup.");
    Reader reader{bytes}; size_t count = reader.number();
    if (count > 10000) throw std::runtime_error("Too many settings records.");
    std::vector<Record> records; std::set<std::pair<std::string, std::string>> names;
    for (size_t i = 0; i < count; ++i) {
        Record r{reader.field(20), reader.field(100), {}};
        if (!allowedName(r) || !names.emplace(r.category, r.name).second) throw std::runtime_error("Invalid or repeated settings record.");
        size_t pairs = reader.number(); if (pairs > 10000) throw std::runtime_error("Too many settings fields.");
        size_t recordBytes = 4;
        for (size_t j = 0; j < pairs; ++j) {
            auto key = reader.field(65536), value = reader.field(4 * 1024 * 1024);
            recordBytes += key.size() * 2 + value.size() * 2 + 2;
            if (key.empty() || !r.pairs.emplace(key, value).second || recordBytes > 16 * 1024 * 1024)
                throw std::runtime_error("Invalid, repeated or oversized settings field.");
        }
        r.pairs.erase("ProxyPassword");
        r.pairs.erase(passwordField); r.pairs.erase(passwordScopeField);
        if (r.category == "sessions") {
            r.pairs["TryAgent"] = "0"; r.pairs["AgentFwd"] = "0"; r.pairs["ConnectionSharing"] = "0";
        }
        records.push_back(std::move(r));
    }
    if (reader.at != bytes.size()) throw std::runtime_error("Unexpected data after settings backup.");
    // Parse and validate the entire file before touching any live records.
    size_t added = 0, kept = 0;
    for (const auto &r : records) {
        auto store = open(r, true);
        if (store->exists && r.category != "trust") { ++kept; continue; }
        bool changed = false;
        for (const auto &[key, value] : r.pairs) {
            if (wsGet(store.get(), key.c_str())) { ++kept; continue; }
            if (!wsSet(store.get(), key.c_str(), value.c_str())) throw std::runtime_error("Import stopped while staging a settings record.");
            changed = true;
        }
        if (changed) {
            if (!wsSave(store.get())) throw std::runtime_error("Import stopped because a record could not be saved. Earlier records may already be imported.");
            ++added;
        }
    }
    return L"Imported " + std::to_wstring(added) + L" records. Kept " + std::to_wstring(kept) +
        L" existing records or trust entries.\n\nExisting names and host keys were preserved. Review imported connection settings before connecting.\n\n"
        L"Passwords are not exported or imported, even in encrypted form. Enter them again for imported hosts.";
}
bool migrateLegacySettings(const std::wstring &source) {
    wchar_t *root = wsRoot();
    if (!root) throw std::runtime_error("Cannot locate the wShell data folder.");
    std::filesystem::path destination(root); free(root);
    const auto marker = destination / L"legacy-data-imported.txt";
    if (std::filesystem::exists(marker) || !std::filesystem::is_directory(std::filesystem::path(source) / L"sessions")) return false;
    std::error_code error;
    if (std::filesystem::equivalent(source, destination, error)) return false;
    auto archive = destination / (L"migration-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".wshell");
    struct TemporaryBackup {
        std::filesystem::path path;
        ~TemporaryBackup() { DeleteFileW(path.c_str()); }
    } cleanup{archive};
    // Used only before the application starts any worker threads or terminal processes.
    struct DataOverride {
        std::wstring previous;
        explicit DataOverride(const std::wstring &path) {
            DWORD length = GetEnvironmentVariableW(L"WOOK_DATA_DIR", nullptr, 0);
            if (length) { previous.resize(length); GetEnvironmentVariableW(L"WOOK_DATA_DIR", previous.data(), length); previous.resize(length - 1); }
            if (!SetEnvironmentVariableW(L"WOOK_DATA_DIR", path.c_str())) throw std::runtime_error("Cannot read legacy settings.");
        }
        ~DataOverride() { SetEnvironmentVariableW(L"WOOK_DATA_DIR", previous.empty() ? nullptr : previous.c_str()); }
    };
    { DataOverride legacy(source); exportSettings(archive.wstring()); }
    importSettings(archive.wstring());
    std::ofstream out(marker, std::ios::binary);
    out << "Imported legacy settings without replacing existing records. The original data folder was preserved.\n";
    out.close();
    if (!out) throw std::runtime_error("Settings were imported, but the migration marker could not be saved.");
    return true;
}
}
