#pragma once
#include <cstdint>
#include <string>
#include <iomanip>
#include <locale>
#include <sstream>

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
// Keep the decimal point and one fractional digit independent of OS locale.
inline std::wstring formatTransferSpeed(double kbps) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(1) << kbps;
    auto text = stream.str();
    const auto decimal = text.find(L'.');
    if (decimal != std::wstring::npos) {
        const size_t start = text.front() == L'-' ? 1 : 0;
        for (size_t position = decimal; position > start + 3;) {
            position -= 3;
            text.insert(position, 1, L',');
        }
    }
    return text;
}
}
