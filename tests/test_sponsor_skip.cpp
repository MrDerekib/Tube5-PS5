// SPDX-License-Identifier: GPL-3.0-or-later
#include "sponsor_skip.hpp"
#include <cassert>
#include <limits>
int main()
{
    tube5::SponsorData data;
    data.state = tube5::SponsorState::ready;
    data.ranges[0] = {0, 4000};
    data.ranges[1] = {809300, 887501};
    data.count = 2;
    tube5::SponsorSkip skip;
    skip.reset(data);
    assert(!skip.target(0, 887.501, false, false, false));
    assert(!skip.target(0, 887.501, true, true, false));
    assert(!skip.target(0, 887.501, true, false, true));
    assert(skip.target(0, 887.501, true, false, false) == 4.0);
    assert(!skip.target(0, 887.501, true, false, false));
    assert(!skip.target(3.9, 887.501, true, false, false));
    assert(!skip.target(4, 887.501, true, false, false));
    assert(skip.target(2, 887.501, true, false, false) == 4.0);
    skip.manual_seek();
    assert(!skip.target(2, 887.501, true, false, true));
    assert(!skip.target(10, 887.501, true, false, false));
    assert(skip.target(809.3, 887.501, true, false, false) == 887.501);
    assert(!skip.target(809.4, 887.501, true, false, false));
    assert(skip.skipped() == 3);
    assert(!skip.target(std::numeric_limits<double>::quiet_NaN(), 887.501, true, false, false));
    assert(!skip.target(809.4, 20, true, false, false));
    skip.reset();
    assert(!skip.target(0, 20, true, false, false));
    assert(skip.skipped() == 0);
    skip.reset(data);
    assert(skip.target(809.3, 887.4, true, false, false) == 887.4);
}
