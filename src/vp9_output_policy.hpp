// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
namespace tube5
{
struct Vp9PendingOutput
{
    std::uint64_t tag = 0;
    bool displayable = true, retained = false;
};
enum class Vp9OutputError { none, empty, flag, ambiguous_hidden, missing_hidden,
                            missing_visible, retained_hidden };
struct Vp9OutputChoice
{
    unsigned input = 0, retire_mask = 0;
    Vp9OutputError error = Vp9OutputError::none;
    bool suppress = false;
};
// No pixels, fixture CRCs or reference timestamps are available to this policy.
// A later visible completion retires only earlier hidden AUs without retained
// surfaces. Hidden surfaces may overtake visible ones, but require a unique tag.
inline Vp9OutputChoice select_vp9_output(std::span<const Vp9PendingOutput> pending,
                                        unsigned indicator) noexcept
{
    const auto count = static_cast<unsigned>(pending.size());
    Vp9OutputChoice choice{count, 0, Vp9OutputError::none};
    if (!count || count > 6)
        return {count, 0, Vp9OutputError::empty};
    if (indicator > 1)
        return {count, 0, Vp9OutputError::flag};
    unsigned first_visible = count;
    for (unsigned i = 0; i < count; ++i)
        if (pending[i].tag && pending[i].displayable &&
            (first_visible == count || pending[i].tag < pending[first_visible].tag))
            first_visible = i;
    for (unsigned i = 0; i < count; ++i)
        if (pending[i].tag && pending[i].displayable == (indicator == 0))
        {
            if (indicator && choice.input != count)
                return {count, 0, Vp9OutputError::ambiguous_hidden};
            if (choice.input == count || pending[i].tag < pending[choice.input].tag)
                choice.input = i;
        }
    // A marked output without a hidden AU can be an auxiliary repeat, not a
    // visible completion. Real-stream signatures match the previous picture.
    // Release that surface without consuming any compressed input/visible PTS.
    if (indicator && choice.input == count && first_visible != count)
        return {count, 0, Vp9OutputError::none, true};
    if (choice.input == count)
        return {count, 0, indicator ? Vp9OutputError::missing_hidden
                                    : Vp9OutputError::missing_visible};
    if (!indicator)
        for (unsigned i = 0; i < count; ++i)
            if (pending[i].tag && !pending[i].displayable &&
                pending[i].tag < pending[choice.input].tag)
            {
                if (pending[i].retained)
                    return {count, 0, Vp9OutputError::retained_hidden};
                choice.retire_mask |= 1u << i;
            }
    return choice;
}
} // namespace tube5
