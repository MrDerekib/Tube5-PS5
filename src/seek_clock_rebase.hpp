// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
namespace tube5
{
// Pinned Kitchensink a2: decoder seeks rebase to PTS-0.1, while the video
// reader schedules PTS+0.1. Align the shared A/V clock once at the first target
// frame, rather than spending 200 ms waiting for that same clock to catch up.
// arm/apply/cancel are serialized by the existing native codec mutex.
class SeekClockRebase
{
    std::atomic<bool> armed_{false};
    std::atomic<std::uint64_t> worker_{0};
    double pts_ = 0;
    unsigned applied_ = 0;
  public:
    void arm(std::uint64_t worker, double pts) noexcept
    {
        pts_ = pts;
        worker_ = worker;
        armed_ = worker != 0 && std::isfinite(pts) && pts >= 0;
    }
    bool eligible(std::uint64_t worker) const noexcept
    {
        return armed_.load() && worker != 0 && worker_.load() == worker;
    }
    double apply(std::uint64_t worker, double value) noexcept
    {
        // Do not rewrite UI pause/seek adjustments, audio follower clocks,
        // arbitrary rebases, or a dependency using a different timing policy.
        if (!eligible(worker) || !std::isfinite(value) ||
            std::abs(value - (pts_ - 0.1)) > 0.000001)
            return value;
        armed_ = false;
        ++applied_;
        return pts_ + 0.1;
    }
    void cancel() noexcept { armed_ = false; }
    void reset() noexcept { cancel(); applied_ = 0; }
    unsigned applied() const noexcept { return applied_; }
};
} // namespace tube5
