#include "launch_relay.hpp"
#include "store.h"
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace wook {
namespace {
constexpr DWORD magic = 0x31534C57; // WLS1, versioned local protocol
constexpr size_t maxChars = 32768;
constexpr DWORD ioTimeout = 5000;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
};
struct SecretText {
    std::wstring text;
    ~SecretText() { if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t)); }
};
struct LocalMemory {
    void *value = nullptr;
    ~LocalMemory() { if (value) LocalFree(value); }
};
std::wstring logonSid(HANDLE process) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) throw std::runtime_error("Cannot identify the launch process.");
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.value, TokenGroups, nullptr, 0, &size);
    if (!size) throw std::runtime_error("Cannot identify the Windows logon.");
    std::vector<BYTE> bytes(size);
    if (!GetTokenInformation(token.value, TokenGroups, bytes.data(), size, &size))
        throw std::runtime_error("Cannot identify the Windows logon.");
    auto groups = reinterpret_cast<TOKEN_GROUPS *>(bytes.data());
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) continue;
        LocalMemory sid;
        if (!ConvertSidToStringSidW(groups->Groups[i].Sid, reinterpret_cast<LPWSTR *>(&sid.value))) break;
        return static_cast<wchar_t *>(sid.value);
    }
    throw std::runtime_error("No interactive logon identity is available.");
}
DWORD integrityLevel() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) throw std::runtime_error("Cannot read process integrity.");
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(token.value, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<BYTE> bytes(size);
    if (!size || !GetTokenInformation(token.value, TokenIntegrityLevel, bytes.data(), size, &size))
        throw std::runtime_error("Cannot read process integrity.");
    auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(bytes.data())->Label.Sid;
    return *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
}
std::wstring storageHash() {
    std::unique_ptr<wchar_t, decltype(&free)> root(wsRoot(), free);
    std::array<wchar_t, 32768> full{};
    DWORD length = root ? GetFullPathNameW(root.get(), (DWORD)full.size(), full.data(), nullptr) : 0;
    if (!length || length >= full.size()) throw std::runtime_error("Cannot identify the settings directory.");
    while (length > 3 && (full[length - 1] == L'\\' || full[length - 1] == L'/')) --length;
    for (DWORD i = 0; i < length; ++i) if (full[i] == L'/') full[i] = L'\\';
    std::wstring normalized(length, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, full.data(), length, normalized.data(), length, nullptr, nullptr, 0))
        throw std::runtime_error("Cannot normalize the settings directory.");
    std::array<BYTE, 32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(normalized.data()),
                   (ULONG)(normalized.size() * sizeof(wchar_t)), digest.data(), (ULONG)digest.size()) < 0)
        throw std::runtime_error("Cannot identify the launch channel.");
    std::wstring hex;
    for (BYTE b : digest) { hex += L"0123456789abcdef"[b >> 4]; hex += L"0123456789abcdef"[b & 15]; }
    return hex;
}
bool samePeer(HANDLE pipe, bool server, const std::wstring &sid) {
    ULONG pid = 0;
    if (!(server ? GetNamedPipeServerProcessId(pipe, &pid) : GetNamedPipeClientProcessId(pipe, &pid))) return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    DWORD ours = 0, theirs = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ours) || !ProcessIdToSessionId(pid, &theirs) || ours != theirs) return false;
    try { return logonSid(process.value) == sid; } catch (...) { return false; }
}
// All I/O is cancellable and bounded, including a client that connects but never
// sends a complete request. Drain cancelled operations before releasing buffers.
bool transfer(HANDLE pipe, HANDLE stop, bool write, void *buffer, DWORD size) {
    auto bytes = static_cast<BYTE *>(buffer);
    const auto deadline = GetTickCount64() + ioTimeout;
    while (size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value) return false;
        OVERLAPPED op{}; op.hEvent = event.value;
        DWORD done = 0;
        BOOL ok = write ? WriteFile(pipe, bytes, size, &done, &op) : ReadFile(pipe, bytes, size, &done, &op);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[] = {stop, event.value};
            auto now = GetTickCount64();
            DWORD wait = WaitForMultipleObjects(2, waits, FALSE, now < deadline ? (DWORD)(deadline - now) : 0);
            if (wait != WAIT_OBJECT_0 + 1) {
                CancelIoEx(pipe, &op); GetOverlappedResult(pipe, &op, &done, TRUE); return false;
            }
            ok = GetOverlappedResult(pipe, &op, &done, FALSE);
        }
        if (!ok || !done) return false;
        bytes += done; size -= done;
    }
    return true;
}
void encode(const LaunchRequest &request, SecretText &wire) {
    wire.text.reserve(maxChars);
    wire.text = L"wShell.exe";
    if (request.mode == LaunchRequest::Mode::workspace) return;
    if (request.mode != LaunchRequest::Mode::connect) throw std::runtime_error("This launch cannot be forwarded.");
    auto append = [&](const wchar_t *option, std::wstring_view value) {
        if (wire.text.size() + wcslen(option) + value.size() * 2 + 4 >= maxChars)
            throw std::runtime_error("Launch request is too large.");
        // Quote directly into the preallocated, scrubbed buffer. Avoid ordinary
        // temporary strings and reallocations containing password fragments.
        wire.text += L" "; wire.text += option; wire.text += L" \"";
        size_t slashes = 0;
        for (wchar_t c : value) {
            if (c == L'\\') { ++slashes; continue; }
            wire.text.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0; wire.text += c;
        }
        wire.text.append(slashes * 2, L'\\'); wire.text += L'"';
    };
    append(L"--host", request.profile.host.find(L':') == std::wstring::npos ? request.profile.host : L"[" + request.profile.host + L"]");
    append(L"--port", std::to_wstring(request.profile.port));
    if (!request.profile.user.empty()) append(L"--user", request.profile.user);
    if (request.named) append(L"--name", request.profile.name);
    if (!request.password.empty()) {
        append(L"--password", {request.password.data(), request.password.size()});
    }
}
}

struct LaunchRelay::State {
    std::wstring sid = logonSid(GetCurrentProcess());
    std::wstring name = L"\\\\.\\pipe\\wShell.Launch.v1." + sid + L"." + std::to_wstring(integrityLevel()) + L"." + storageHash();
    Handle stopEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle pipe;
    std::thread worker;
    std::mutex mutex;
    std::deque<std::unique_ptr<LaunchRequest>> queue;
    HWND window = nullptr;

    State() {
        if (!stopEvent.value) throw std::runtime_error("Cannot initialize launch forwarding.");
        LocalMemory descriptor;
        auto sddl = L"D:P(A;;GA;;;" + sid + L")";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                reinterpret_cast<PSECURITY_DESCRIPTOR *>(&descriptor.value), nullptr))
            throw std::runtime_error("Cannot secure the launch channel.");
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor.value, FALSE};
        pipe.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &security);
        // The first pipe instance elects the workspace owner atomically. Existing
        // instances are contacted, never silently replaced on a delivery error.
        if (pipe.value == INVALID_HANDLE_VALUE && GetLastError() != ERROR_ACCESS_DENIED && GetLastError() != ERROR_PIPE_BUSY)
            throw std::runtime_error("Cannot create the launch channel.");
    }
    void serve() {
        while (WaitForSingleObject(stopEvent.value, 0) == WAIT_TIMEOUT) {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event.value) return;
            OVERLAPPED op{}; op.hEvent = event.value;
            BOOL connected = ConnectNamedPipe(pipe.value, &op);
            DWORD error = connected ? ERROR_SUCCESS : GetLastError(), done = 0;
            if (error == ERROR_IO_PENDING) {
                HANDLE waits[] = {stopEvent.value, event.value};
                if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(pipe.value, &op); GetOverlappedResult(pipe.value, &op, &done, TRUE); return;
                }
                connected = GetOverlappedResult(pipe.value, &op, &done, FALSE);
            } else connected = connected || error == ERROR_PIPE_CONNECTED;
            if (!connected) { DisconnectNamedPipe(pipe.value); continue; }
            DWORD ack = 0;
            try {
                DWORD header[2]{};
                if (!samePeer(pipe.value, false, sid) || !transfer(pipe.value, stopEvent.value, false, header, sizeof(header)) ||
                    header[0] != magic || header[1] < 2 || header[1] > maxChars * sizeof(wchar_t) || header[1] % sizeof(wchar_t))
                    throw std::runtime_error("Invalid launch packet.");
                SecretText wire; wire.text.resize(header[1] / sizeof(wchar_t));
                if (!transfer(pipe.value, stopEvent.value, false, wire.text.data(), header[1]) || wire.text.back() != L'\0' ||
                    std::find(wire.text.begin(), wire.text.end() - 1, L'\0') != wire.text.end() - 1)
                    throw std::runtime_error("Incomplete launch packet.");
                auto request = std::make_unique<LaunchRequest>(parseLaunchCommand(wire.text.c_str()));
                if (request->passwordStdin || (request->mode != LaunchRequest::Mode::connect && request->mode != LaunchRequest::Mode::workspace))
                    throw std::runtime_error("Unsupported relayed launch.");
                std::lock_guard lock(mutex);
                if (queue.size() < 16 && WaitForSingleObject(stopEvent.value, 0) == WAIT_TIMEOUT) {
                    queue.push_back(std::move(request));
                    if (PostMessageW(window, launchRelayMessage, 0, 0)) ack = 1;
                    else queue.pop_back();
                }
            } catch (...) { /* Never log request content or credentials. */ }
            if (transfer(pipe.value, stopEvent.value, true, &ack, sizeof(ack))) {
                // A read receipt prevents DisconnectNamedPipe discarding an unread acknowledgement.
                DWORD receipt = 0; transfer(pipe.value, stopEvent.value, false, &receipt, sizeof(receipt));
            }
            DisconnectNamedPipe(pipe.value);
        }
    }
};

LaunchRelay::LaunchRelay() : state_(std::make_unique<State>()) {}
LaunchRelay::~LaunchRelay() { stop(); }
bool LaunchRelay::forward(const LaunchRequest &request) {
    if (state_->pipe.value != INVALID_HANDLE_VALUE) return false;
    Handle client;
    auto deadline = GetTickCount64() + 15000;
    do {
        client.value = CreateFileW(state_->name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (client.value != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(state_->name.c_str(), 200);
    } while (GetTickCount64() < deadline);
    if (client.value == INVALID_HANDLE_VALUE || !samePeer(client.value, true, state_->sid))
        throw std::runtime_error("Cannot reach the existing wShell window. Close it or retry after it has finished starting.");
    ULONG pid = 0;
    if (GetNamedPipeServerProcessId(client.value, &pid)) AllowSetForegroundWindow(pid);
    SecretText wire; encode(request, wire);
    DWORD header[] = {magic, (DWORD)((wire.text.size() + 1) * sizeof(wchar_t))}, ack = 0;
    if (!transfer(client.value, state_->stopEvent.value, true, header, sizeof(header)) ||
        !transfer(client.value, state_->stopEvent.value, true, wire.text.data(), header[1]) ||
        !transfer(client.value, state_->stopEvent.value, false, &ack, sizeof(ack)))
        throw std::runtime_error("Launch delivery could not be confirmed. Check the existing window before retrying.");
    DWORD receipt = 1; transfer(client.value, state_->stopEvent.value, true, &receipt, sizeof(receipt));
    if (ack != 1) throw std::runtime_error("The existing wShell window rejected the launch request.");
    return true;
}
void LaunchRelay::start(HWND window) {
    if (state_->pipe.value == INVALID_HANDLE_VALUE || state_->worker.joinable()) throw std::runtime_error("Invalid launch listener state.");
    state_->window = window;
    state_->worker = std::thread([this] { state_->serve(); });
}
std::unique_ptr<LaunchRequest> LaunchRelay::take() {
    std::lock_guard lock(state_->mutex);
    if (state_->queue.empty()) return {};
    auto request = std::move(state_->queue.front()); state_->queue.pop_front(); return request;
}
void LaunchRelay::stop() {
    if (!state_) return;
    SetEvent(state_->stopEvent.value);
    if (state_->worker.joinable()) state_->worker.join();
    std::lock_guard lock(state_->mutex); state_->queue.clear();
}
}
