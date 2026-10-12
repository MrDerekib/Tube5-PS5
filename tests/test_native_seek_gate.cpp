// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_seek_gate.hpp"
#include <cassert>
int main()
{
    tube5::SeekPreroll gate;
    assert(gate.accept(10, 0));
    gate.begin(5000, 3);
    assert(gate.accept(9000, 3) && gate.waiting()); // old generation cannot complete a seek
    assert(!gate.accept(2000, 4) && gate.waiting());
    assert(!gate.accept(4999, 4));
    assert(gate.accept(5000, 4) && !gate.waiting());
    gate.begin(1000, 4); // backward seek
    assert(gate.accept(9000, 4) && gate.waiting());
    assert(!gate.accept(0, 5));
    assert(gate.accept(1001, 5) && !gate.waiting());
    gate.begin(7000, 5);
    gate.begin(8000, 5); // replace pending target
    assert(!gate.accept(7000, 6));
    gate.clear();
    assert(!gate.waiting() && gate.accept(0, 6));
}
