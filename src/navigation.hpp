#pragma once
#include <algorithm>

namespace wook {
inline constexpr int navigationDefaultWidth = 252;
inline constexpr int navigationMinWidth = 220;
inline constexpr int navigationMaxWidth = 520;
inline constexpr int workspaceMinWidth = 700;

// Preferences use logical pixels; fitting a small window never overwrites them.
inline int navigationWidth(int requested, int clientWidth) {
    int maximum = std::clamp(clientWidth - workspaceMinWidth, navigationMinWidth, navigationMaxWidth);
    return std::clamp(requested, navigationMinWidth, maximum);
}
}
