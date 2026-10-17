#pragma once
#include <cstdint>
#include <string>

namespace wook {
// Display-only formatting: no floating-point conversion or locale dependency.
inline std::wstring formatInteger(uint64_t value) {
    auto text = std::to_wstring(value);
    for (size_t position = text.size(); position > 3;) {
        position -= 3;
        text.insert(position, 1, L',');
    }
    return text;
}
}
