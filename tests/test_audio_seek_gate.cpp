// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_seek_gate.hpp"
#include <cassert>
int main()
{
    tube5::AudioSeekGate gate;
    assert(!gate.waiting());
    gate.begin(10, 4);
    assert(gate.waiting());
    assert(!gate.presented(10, 4) && gate.waiting()); // Old queued frame.
    assert(gate.waiting()); // Reset without presentation cannot resume PCM.
    assert(!gate.presented(11, 4) && gate.waiting()); // Audio hasn't reset yet.
    assert(gate.presented(11, 5) && !gate.waiting());
    gate.begin(11, 5);              // Backward seek uses the same barrier.
    assert(!gate.presented(11, 6)); // Audio finishes first, old video remains.
    assert(gate.presented(12, 6) && !gate.waiting());
    gate.begin(12, 6);
    gate.clear(); // Failure / close.
    assert(!gate.waiting() && !gate.presented(13, 7));
}
