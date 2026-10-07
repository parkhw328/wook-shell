#pragma once
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace wook {
struct PaneRect { int x, y, width, height; };
// Two columns; three panes use a full-height left pane; four use a 2x2 grid.
inline std::vector<PaneRect> splitRects(int count, int x, int y, int width, int height, int gap = 6) {
    count = std::clamp(count, 1, 4);
    if (count == 1) return {{x, y, width, height}};
    int left = (width - gap) / 2, right = width - gap - left;
    int top = (height - gap) / 2, bottom = height - gap - top;
    if (count == 2) return {{x,y,left,height}, {x+left+gap,y,right,height}};
    if (count == 3) return {{x,y,left,height}, {x+left+gap,y,right,top}, {x+left+gap,y+top+gap,right,bottom}};
    return {{x,y,left,top}, {x+left+gap,y,right,top}, {x,y+top+gap,left,bottom}, {x+left+gap,y+top+gap,right,bottom}};
}
// Direction: 0 left, 1 up, 2 right, 3 down. Prefer the nearest aligned pane.
inline int adjacentPane(const std::vector<PaneRect> &rects, int current, int direction) {
    if (current < 0 || current >= (int)rects.size() || direction < 0 || direction > 3) return current;
    auto from = rects[current]; int best = current, bestScore = 0x7fffffff;
    for (int i = 0; i < (int)rects.size(); ++i) {
        if (i == current) continue;
        auto to = rects[i]; int dx = 2 * (to.x - from.x) + to.width - from.width;
        int dy = 2 * (to.y - from.y) + to.height - from.height;
        int forward = direction == 0 ? -dx : direction == 1 ? -dy : direction == 2 ? dx : dy;
        if (forward <= 0) continue;
        int across = direction % 2 == 0 ? abs(dy) : abs(dx);
        int score = forward + 2 * across;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}
}
