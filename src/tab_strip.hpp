#pragma once
#include <algorithm>

namespace wook {
struct TabStrip {
    int capacity = 1, width = 198;
    bool overflow = false;
};
// Width excludes Workspace, the all-tabs menu and the new-tab button.
inline TabStrip tabStrip(int available, int count) {
    TabStrip result;
    count = std::max(1, count);
    result.overflow = count > std::max(1, available / 112);
    if (result.overflow) available -= 72; // Two visible scroll buttons.
    result.capacity = std::min(count, std::max(1, available / 112));
    result.width = std::max(1, std::min(198, available / result.capacity));
    return result;
}
inline int tabStripStart(int first, int count, int capacity, int reveal = -1) {
    if (reveal >= 0) first = std::clamp(first, std::max(0, reveal - capacity + 1), reveal);
    return std::clamp(first, 0, std::max(0, count - capacity));
}
}
