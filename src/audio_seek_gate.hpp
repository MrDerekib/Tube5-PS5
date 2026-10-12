// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
namespace tube5
{
// UI owns generation; audio only reads waiting(). Old queued frames cannot
// finish a seek: a decoder reset and a new presented frame are both required.
class AudioSeekGate
{
    std::atomic<bool> waiting_{false};
    unsigned generation_ = 0, audio_generation_ = 0;

  public:
    void begin(unsigned generation, unsigned audio_generation) noexcept
    {
        generation_ = generation;
        audio_generation_ = audio_generation;
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
    bool video_reset_ready(unsigned generation) const noexcept
    {
        return !waiting() || generation != generation_;
    }
    bool presented(unsigned generation, unsigned audio_generation) noexcept
    {
        if (!waiting() || generation == generation_ || audio_generation == audio_generation_)
            return false;
        clear();
        return true;
    }
};
} // namespace tube5
