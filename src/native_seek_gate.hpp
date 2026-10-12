// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
namespace tube5
{
// Native request/accept run under the codec mutex. UI/audio read waiting().
class SeekPreroll
{
    std::int64_t target_ = 0;
    unsigned generation_ = 0;
    std::atomic<bool> waiting_{false};

  public:
    void begin(std::int64_t target, unsigned generation) noexcept
    {
        target_ = target;
        generation_ = generation;
        waiting_ = true;
    }
    void clear() noexcept
    {
        waiting_ = false;
    }
    bool waiting() const noexcept
    {
        return waiting_.load();
    }
    bool accept(std::int64_t pts, unsigned generation) noexcept
    {
        if (!waiting() || generation == generation_)
            return true; // Old decoded frames still drain; UI must not present them.
        if (pts < target_)
            return false;
        clear();
        return true;
    }
};
} // namespace tube5
