#pragma once
#include <algorithm>
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
}
