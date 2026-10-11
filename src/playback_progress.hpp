// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>

namespace tube5
{
// UI-only geometry: no allocation or changes to playback/seek timing.
inline unsigned playback_progress_width(double position, double duration, unsigned width) noexcept
{
    if (!width || !std::isfinite(position) || !std::isfinite(duration) || duration <= 0.0 ||
        position <= 0.0)
        return 0;
    if (position >= duration)
        return width;
    return static_cast<unsigned>((position / duration) * width);
}
} // namespace tube5
