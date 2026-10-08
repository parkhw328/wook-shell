#include "launch_relay.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *what) { if (!ok) throw std::runtime_error(what); }
struct Process {
    HANDLE handle = nullptr;
    ~Process() {
        if (handle) { if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) TerminateProcess(handle, 1); CloseHandle(handle); }
    }
    void start(const std::wstring &exe, const wchar_t *option) {
        auto command = wook::quoteArg(exe) + L" " + option;
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION child{};
        check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &child), "Start relay client");
        handle = child.hProcess; CloseHandle(child.hThread);
    }
    void wait() {
        check(WaitForSingleObject(handle, 20000) == WAIT_OBJECT_0, "Relay client timeout");
        DWORD code = 1; GetExitCodeProcess(handle, &code); check(code == 0, "Relay client failed");
    }
};
const std::wstring password = L"fixture 한글 & | \\\" end\\";
wook::LaunchRequest request() {
    std::wstring_view args[] = {L"-ssh", L"user@[::1]", L"-P", L"2222", L"--name", L"relay 서울", L"-pw", password};
    return wook::parseLaunchArguments(args);
}
}
int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 2) {
            wook::LaunchRelay relay;
            auto launch = std::wstring_view(argv[1]) == L"--activate" ? wook::LaunchRequest{} : request();
            check(relay.forward(launch), "Client must find the existing listener");
            return 0;
        }
        wchar_t cwd[32768]{}, exe[32768]{};
        check(GetCurrentDirectoryW(32768, cwd) && GetModuleFileNameW(nullptr, exe, 32768), "Test paths");
        auto folder = std::filesystem::path(cwd) / (L"relay-test-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(folder);
        auto data = (folder / L"data").wstring();
        SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
        HWND window = CreateWindowExW(0, L"STATIC", L"relay fixture", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        check(window != nullptr, "Create notification window");
        {
            wook::LaunchRelay owner;
            check(!owner.forward(wook::LaunchRequest{}), "First process owns workspace");
            // Requests can arrive before the first workspace has finished startup.
            Process early; early.start(exe, L"--connect");
            Sleep(100); owner.start(window); early.wait();
            auto received = owner.take();
            check(received && received->mode == wook::LaunchRequest::Mode::connect && received->named, "Receive connection");
            check(received->profile.host == L"::1" && received->profile.port == 2222 && received->profile.user == L"user" &&
                  received->profile.name == L"relay 서울", "Preserve endpoint and Unicode name");
            check(std::wstring(received->password.begin(), received->password.end()) == password, "Preserve Unicode and quoted password");
            received.reset();
            check(!owner.take(), "Deliver exactly once");
            Process concurrent[4];
            for (auto &p : concurrent) p.start(exe, L"--connect");
            for (auto &p : concurrent) p.wait();
            for (int i = 0; i < 4; ++i) check(owner.take() != nullptr, "Concurrent requests are not dropped");
            check(!owner.take(), "No duplicate concurrent requests");
            // Process image name is deliberately excluded from channel identity.
            auto renamed = (folder / L"putty.exe").wstring();
            std::filesystem::copy_file(exe, renamed, std::filesystem::copy_options::overwrite_existing);
            Process alias; alias.start(renamed, L"--connect"); alias.wait();
            check(owner.take() != nullptr, "Renamed executable finds same workspace");
            Process focus; focus.start(exe, L"--activate"); focus.wait();
            auto activation = owner.take();
            check(activation && activation->mode == wook::LaunchRequest::Mode::workspace, "Empty launch activates without connecting");
            auto other = (folder / L"other-data").wstring();
            SetEnvironmentVariableW(L"WOOK_DATA_DIR", other.c_str());
            { wook::LaunchRelay isolated; check(!isolated.forward(wook::LaunchRequest{}), "Separate data directory owns separate workspace"); }
            SetEnvironmentVariableW(L"WOOK_DATA_DIR", data.c_str());
            // The queue is bounded; a rejected request must not appear later.
            for (int i = 0; i < 16; ++i) {
                wook::LaunchRelay client; check(client.forward(wook::LaunchRequest{}), "Fill pending queue");
            }
            bool rejected = false;
            try { wook::LaunchRelay client; client.forward(wook::LaunchRequest{}); } catch (const std::exception &) { rejected = true; }
            check(rejected, "Full queue rejects explicitly");
            for (int i = 0; i < 16; ++i) check(owner.take() != nullptr, "Drain accepted requests");
            check(!owner.take(), "Rejected request not queued");
            auto before = GetTickCount64(); owner.stop();
            check(GetTickCount64() - before < 2000, "Pending accept is cancelled on shutdown");
        }
        { wook::LaunchRelay replacement; check(!replacement.forward(wook::LaunchRequest{}), "Closed owner releases channel"); }
        DestroyWindow(window);
        std::cout << "PASS: startup, concurrent forwarding, quoted Unicode password, renamed EXE, activation, storage isolation, queue bound, shutdown\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
