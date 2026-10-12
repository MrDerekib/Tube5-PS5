// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tube5
{
// One active player. Call only after SDL has paused/joined its audio callback,
// before seeking or destroying the decoder. No extra PCM allocation is needed.
void reset_audio_reader() noexcept;
// During a separate-stream seek discard only pre-reset PCM (untimed) so a
// blocked producer can process its seek marker. Preserve post-reset PCM until
// video's new clock is ready. Call after pausing the SDL callback.
void hold_audio_reader(unsigned audio_reset_generation) noexcept;
void resume_audio_reader() noexcept;
} // namespace tube5
