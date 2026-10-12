// SPDX-License-Identifier: GPL-3.0-or-later
#include "vp9_output_policy.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main()
{
    using namespace tube5;
    std::array<Vp9PendingOutput, 4> pending{{{3,true,true}, {2,false,false},
                                           {4,true,false}, {}}};
    auto choice = select_vp9_output(pending, 0);
    assert(choice.error == Vp9OutputError::none && choice.input == 0 && choice.retire_mask == 2);
    choice = select_vp9_output(pending, 1);
    assert(choice.error == Vp9OutputError::none && choice.input == 1 && !choice.retire_mask);
    pending[1].tag = 5; // Hidden surface overtakes an older visible completion.
    choice = select_vp9_output(pending, 1);
    assert(choice.input == 1 && choice.error == Vp9OutputError::none);
    choice = select_vp9_output(pending, 0);
    assert(choice.input == 0 && !choice.retire_mask);
    pending[3] = {6,false,false};
    assert(select_vp9_output(pending, 1).error == Vp9OutputError::ambiguous_hidden);
    pending[3] = {};
    pending[1] = {2,false,true};
    assert(select_vp9_output(pending, 0).error == Vp9OutputError::retained_hidden);
    assert(select_vp9_output(pending, 2).error == Vp9OutputError::flag);
    pending = {};
    assert(select_vp9_output(pending, 0).error == Vp9OutputError::missing_visible);
    assert(select_vp9_output(pending, 1).error == Vp9OutputError::missing_hidden);
    assert(select_vp9_output({}, 0).error == Vp9OutputError::empty);
    std::puts("VP9 flag-only output policy, FIFO, hidden omission, overtaking and guards passed");
}
