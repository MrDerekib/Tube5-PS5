// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
namespace tube5
{
struct SeekDecodeTiming
{
    std::uint64_t prepared = 0, reset = 0, packet = 0, decoded = 0;
    std::uint64_t decode_us = 0;
    unsigned skipped = 0, calls = 0;
};
// Writers run under the existing codec mutex; UI reads atomics without waiting
// for a decode/reset. A new prepare replaces the previous measurement.
class SeekDecodeTimer
{
    std::atomic<std::uint64_t> prepared_{0}, reset_{0}, packet_{0}, decoded_{0}, us_{0};
    std::atomic<unsigned> skipped_{0}, calls_{0};
    bool active_ = false;
    unsigned generation_ = 0, base_calls_ = 0;
    std::uint64_t base_us_ = 0;
  public:
    void begin(std::uint64_t now, unsigned generation) noexcept
    {
        active_ = true;
        generation_ = generation;
        prepared_ = now;
        reset_ = packet_ = decoded_ = us_ = 0;
        skipped_ = calls_ = 0;
    }
    void reset(std::uint64_t now, unsigned generation, unsigned calls,
               std::uint64_t us) noexcept
    {
        if (!active_ || generation == generation_ || reset_.load()) return;
        base_calls_ = calls;
        base_us_ = us;
        reset_ = now;
    }
    void packet(std::uint64_t now) noexcept
    {
        if (active_ && reset_.load() && !packet_.load()) packet_ = now;
    }
    void skip() noexcept
    {
        if (active_ && reset_.load()) ++skipped_;
    }
    void decoded(std::uint64_t now, unsigned calls, std::uint64_t us) noexcept
    {
        if (!active_ || !reset_.load()) return;
        calls_ = calls - base_calls_;
        us_ = us - base_us_;
        decoded_ = now;
        active_ = false;
    }
    void cancel() noexcept { active_ = false; }
    SeekDecodeTiming snapshot() const noexcept
    {
        return {prepared_.load(), reset_.load(), packet_.load(), decoded_.load(),
                us_.load(), skipped_.load(), calls_.load()};
    }
};
struct SeekLatencyHistory
{
    struct Record
    {
        std::uint64_t requested = 0, dispatch = 0, returned = 0, presented = 0, audio = 0;
        std::int64_t target_ms = 0;
        SeekDecodeTiming native{};
        unsigned status = 0; // 0 pending, 1 presented, 2 failed, 3 superseded.
    } records[16];
    unsigned count = 0;
    Record *current() noexcept { return count ? &records[(count - 1) % 16] : nullptr; }
    void begin(std::uint64_t request, std::uint64_t dispatch, std::int64_t target) noexcept
    {
        if (auto *prior = current(); prior && !prior->status) prior->status = 3;
        auto &r = records[count++ % 16];
        r = {};
        r.requested = request;
        r.dispatch = dispatch;
        r.target_ms = target;
    }
    std::size_t format(char *out, std::size_t capacity, const char *label) const noexcept
    {
        if (!capacity) return 0;
        std::size_t used = 0;
        const auto append = [&](const char *pattern, auto... args)
        {
            if (used == capacity - 1) return;
            const int n = std::snprintf(out + used, capacity - used, pattern, args...);
            if (n > 0) used += std::min<std::size_t>(n, capacity - used - 1);
        };
        append("TUBE5 SEEK LATENCY V1\nBUILD %s\nTOTAL %u\n"
               "All phases US from dispatch (-1 unavailable); WAIT from last request.\n"
               "N TARGET_MS STATE WAIT PREP RETURN RESET PACKET DECODED UPLOAD AUDIO SKIP CALLS HW_US\n"
               "STATE 0 pending/closed, 1 uploaded, 2 failed, 3 superseded; UPLOAD not HDMI flip.\n",
               label, count);
        const unsigned start = count > 16 ? count - 16 : 0;
        for (unsigned i = start; i < count; ++i)
        {
            const auto &r = records[i % 16];
            const auto delta = [&](std::uint64_t time) -> long long
            { return time && time >= r.dispatch ? static_cast<long long>(time - r.dispatch) : -1; };
            append("%u %lld %u %lld %lld %lld %lld %lld %lld %lld %lld %u %u %llu\n",
                   i + 1, static_cast<long long>(r.target_ms), r.status,
                   r.requested && r.dispatch >= r.requested ?
                       static_cast<long long>(r.dispatch - r.requested) : -1,
                   delta(r.native.prepared), delta(r.returned), delta(r.native.reset),
                   delta(r.native.packet), delta(r.native.decoded), delta(r.presented),
                   delta(r.audio), r.native.skipped, r.native.calls,
                   static_cast<unsigned long long>(r.native.decode_us));
        }
        return used;
    }
};
} // namespace tube5
