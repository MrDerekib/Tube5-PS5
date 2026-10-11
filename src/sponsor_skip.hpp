// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <algorithm>
namespace tube5
{
enum class SponsorState
{
    none,
    ready,
    pending,
    unavailable,
    unaligned
};
struct SponsorRange
{
    unsigned start_ms = 0, end_ms = 0;
};
struct SponsorData
{
    SponsorState state = SponsorState::none;
    std::array<SponsorRange, 8> ranges{};
    unsigned count = 0;
};
// One request per encounter; a delayed/failed seek must never create a loop.
// Rewinding into a segment deliberately permits skipping it again.
class SponsorSkip
{
  public:
    void reset(const SponsorData &data = {}) noexcept
    {
        data_ = data;
        latched_ = -1;
        skipped_ = 0;
    }
    void manual_seek() noexcept
    {
        latched_ = -1;
    }
    unsigned skipped() const noexcept
    {
        return skipped_;
    }
    const SponsorData &data() const noexcept
    {
        return data_;
    }
    std::optional<double> target(double position, double duration, bool enabled, bool paused,
                                 bool busy) noexcept
    {
        if (!std::isfinite(position) || !std::isfinite(duration) || position < 0 || duration <= 0)
            return std::nullopt;
        if (latched_ >= 0)
        {
            const auto &range = data_.ranges[static_cast<unsigned>(latched_)];
            if (position < range.start_ms / 1000.0 || position >= range.end_ms / 1000.0)
                latched_ = -1;
        }
        if (!enabled || paused || busy || data_.state != SponsorState::ready)
            return std::nullopt;
        for (unsigned i = 0; i < data_.count && i < data_.ranges.size(); ++i)
        {
            const double start = data_.ranges[i].start_ms / 1000.0;
            const double end = data_.ranges[i].end_ms / 1000.0;
            if (end <= start || start >= duration || end > duration + 2.0)
                continue;
            if (position >= start && position < std::min(end, duration) - 0.01 &&
                latched_ != static_cast<int>(i))
            {
                latched_ = static_cast<int>(i);
                ++skipped_;
                return std::min(end, duration);
            }
        }
        return std::nullopt;
    }

  private:
    SponsorData data_{};
    int latched_ = -1;
    unsigned skipped_ = 0;
};
} // namespace tube5
