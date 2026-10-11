// SPDX-License-Identifier: GPL-3.0-or-later
#include "seek_selection.hpp"
#include <cassert>
#include <limits>

int main()
{
    tube5::SeekSelection selection;
    assert(!selection.active());
    assert(!selection.confirm(600));
    assert(selection.begin(100, 600));
    selection.move(30);
    selection.move(5);
    assert(selection.target() == 135);
    // Playback advanced while choosing: seek to 135, not 155.
    auto destination = selection.confirm(600);
    assert(destination && *destination == 135);
    assert(!selection.active());
    assert(!selection.confirm(600)); // Exactly one commit.
    assert(selection.begin(100, 600));
    selection.move(-30);
    selection.cancel();
    assert(!selection.confirm(600));
    assert(selection.begin(-5, 600));
    selection.move(-30);
    assert(selection.target() == 0);
    selection.move(1000);
    assert(selection.target() == 600);
    destination = selection.confirm(300); // Duration changed before confirmation.
    assert(destination && *destination == 300);
    assert(selection.begin(700, 600));
    assert(selection.target() == 600);
    assert(selection.begin(60, 600)); // Paused clock: selection never changes it.
    selection.move(-5);
    destination = selection.confirm(600);
    assert(destination && *destination == 55);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (auto invalid : {0.0, -1.0, nan, inf})
        assert(!selection.begin(5, invalid));
    assert(!selection.begin(nan, 600));
    assert(selection.begin(5, 600));
    selection.move(nan);
    selection.move(inf);
    assert(selection.target() == 5);
    assert(!selection.confirm(nan));
    assert(!selection.active());
    selection.move(30);
    assert(selection.target() == 0);
}
