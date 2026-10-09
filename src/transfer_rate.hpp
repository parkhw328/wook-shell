#pragma once
#include <cstdint>
#include <deque>

namespace sftp {
// Sample on the UI timer, including idle ticks, so stalled transfers reach zero.
class TransferRate {
    struct Sample { uint64_t time, bytes; };
    std::deque<Sample> samples;
public:
    void reset(uint64_t now) { samples.clear(); samples.push_back({now, 0}); }
    double update(uint64_t now, uint64_t bytes) {
        if (samples.empty() || now < samples.back().time || bytes < samples.back().bytes) reset(now);
        if (now == samples.back().time) return 0;
        samples.push_back({now, bytes});
        while (samples.size() > 2 && now - samples[1].time >= 1000) samples.pop_front();
        const auto first = samples.front();
        return (double)(bytes - first.bytes) * 1000.0 / (now - first.time) / 1024.0;
    }
};
}
