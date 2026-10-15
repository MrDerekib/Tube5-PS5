// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
namespace tube5
{
// Numeric diagnostics only: no URLs, compressed payloads or frame dumps.
struct Vp9OutputEvent
{
    struct Pending
    {
        std::uint64_t tag = 0, pts = 0, dts = 0;
        bool visible = false, retained = false;
    } pending[6];
    std::uint64_t owners[6]{}, submitted = 0, signature = 0;
    unsigned bytes = 0, prefix = 0, offered = 0, accepted = 0, returned = 0;
    unsigned valid = 0, native_error = 0, indicator = 0;
    unsigned selected = 6, retire = 0, policy = 0;
    bool draining = false;
    bool suppressed = false;
};
struct Vp9OutputTrace
{
    Vp9OutputEvent events[64]{};
    std::uint64_t count = 0;
    Vp9OutputEvent &push(const Vp9OutputEvent &event) noexcept
    {
        auto &stored = events[count++ % 64];
        stored = event;
        return stored;
    }
    std::size_t format(char *out, std::size_t capacity, const char *label,
                       const char *stage, int error) const noexcept
    {
        if (!capacity) return 0;
        std::size_t used = 0;
        out[0] = 0;
        const auto append = [&](const char *pattern, auto... args)
        {
            if (used == capacity - 1) return;
            const int n = std::snprintf(out + used, capacity - used, pattern, args...);
            if (n > 0) used += std::min<std::size_t>(n, capacity - used - 1);
        };
        append("TUBE5 VP9 OUTPUT TRACE V1\nBUILD %s\nSTAGE %s ERROR %d TOTAL %llu\n"
               "SIG FNV1A64 Y: 8x8 cell centers; pending slot:tag/pts/dts/visible/retained\n",
               label, stage, error, static_cast<unsigned long long>(count));
        const auto start = count > 64 ? count - 64 : 0;
        for (auto index = start; index < count; ++index)
        {
            const auto &e = events[index % 64];
            append("EVENT %llu IN %llu BYTES %u PREFIX %08X DRAIN %u OFFER %u ACCEPT %u "
                   "RETURN %u VALID %u ERR %u FLAG %u SIG %016llX SELECT %u RETIRE %02X POLICY %u SUPPRESS %u\n",
                   static_cast<unsigned long long>(index + 1),
                   static_cast<unsigned long long>(e.submitted), e.bytes, e.prefix,
                   e.draining ? 1u : 0u, e.offered, e.accepted, e.returned, e.valid,
                   e.native_error, e.indicator, static_cast<unsigned long long>(e.signature),
                   e.selected, e.retire, e.policy, e.suppressed ? 1u : 0u);
            for (unsigned i = 0; i < 6; ++i)
            {
                const auto &p = e.pending[i];
                if (p.tag)
                    append("P %u:%llu/%llu/%llu/%u/%u\n", i,
                           static_cast<unsigned long long>(p.tag),
                           static_cast<unsigned long long>(p.pts),
                           static_cast<unsigned long long>(p.dts),
                           p.visible ? 1u : 0u, p.retained ? 1u : 0u);
            }
            append("OWNERS %llu %llu %llu %llu %llu %llu\n",
                   static_cast<unsigned long long>(e.owners[0]),
                   static_cast<unsigned long long>(e.owners[1]),
                   static_cast<unsigned long long>(e.owners[2]),
                   static_cast<unsigned long long>(e.owners[3]),
                   static_cast<unsigned long long>(e.owners[4]),
                   static_cast<unsigned long long>(e.owners[5]));
        }
        return used;
    }
};
} // namespace tube5
