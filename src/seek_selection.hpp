// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace tube5
{
// A virtual absolute destination. No media calls until confirm; playback can
// continue while selecting; confirmation returns the absolute destination.
class SeekSelection
{
  public:
    bool begin(double position, double duration) noexcept
    {
        cancel();
        if (!std::isfinite(position) || !std::isfinite(duration) || duration <= 0)
            return false;
        duration_ = duration;
        target_ = std::clamp(position, 0.0, duration);
        active_ = true;
        return true;
    }
    void move(double delta) noexcept
    {
        if (active_ && std::isfinite(delta))
            target_ = std::clamp(target_ + delta, 0.0, duration_);
    }
    std::optional<double> confirm(double duration) noexcept
    {
        if (!active_)
            return std::nullopt;
        const double destination = target_;
        cancel();
        if (!std::isfinite(duration) || duration <= 0)
            return std::nullopt;
        return std::clamp(destination, 0.0, duration);
    }
    void cancel() noexcept
    {
        active_ = false;
        target_ = duration_ = 0;
    }
    bool active() const noexcept
    {
        return active_;
    }
    double target() const noexcept
    {
        return target_;
    }

  private:
    bool active_ = false;
    double target_ = 0, duration_ = 0;
};
} // namespace tube5
