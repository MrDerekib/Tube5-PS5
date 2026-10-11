// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace tube5
{
class SponsorNotice
{
  public:
    void begin(double from, double to) noexcept
    {
        cancel();
        if (!std::isfinite(from) || !std::isfinite(to) || to <= from)
            return;
        seconds_ = static_cast<unsigned>(std::clamp(std::round(to - from), 1.0, 3600.0));
        pending_ = true;
    }
    void complete(std::uint32_t now) noexcept
    {
        if (!pending_)
            return;
        pending_ = false;
        shown_ = true;
        started_ = now;
    }
    void cancel() noexcept
    {
        pending_ = shown_ = false;
    }
    bool visible(std::uint32_t now) const noexcept
    {
        return shown_ && static_cast<std::uint32_t>(now - started_) < 2500;
    }
    unsigned seconds() const noexcept
    {
        return seconds_;
    }

  private:
    bool pending_ = false, shown_ = false;
    std::uint32_t started_ = 0;
    unsigned seconds_ = 0;
};
} // namespace tube5
