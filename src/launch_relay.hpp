#pragma once
#include "launch.hpp"
#include <memory>

namespace wook {
// Only a wake-up notification crosses the window message boundary. Credentials
// travel through a logon-scoped named pipe, never WM_COPYDATA or a command line.
inline constexpr UINT launchRelayMessage = WM_APP + 90;
class LaunchRelay {
    struct State;
    std::unique_ptr<State> state_;
public:
    LaunchRelay();
    ~LaunchRelay();
    LaunchRelay(const LaunchRelay &) = delete;
    LaunchRelay &operator=(const LaunchRelay &) = delete;
    bool forward(const LaunchRequest &request);
    void start(HWND window);
    std::unique_ptr<LaunchRequest> take();
    void stop();
};
}
