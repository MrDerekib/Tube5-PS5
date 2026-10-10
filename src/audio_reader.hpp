// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tube5
{
// One active player. Call only after SDL has paused/joined its audio callback,
// before seeking or destroying the decoder. No extra PCM allocation is needed.
void reset_audio_reader() noexcept;
} // namespace tube5
