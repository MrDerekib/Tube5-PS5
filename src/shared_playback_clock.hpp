// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <kitchensink2/kitchensink.h>
namespace tube5
{
// UI-thread construction only, before either decoder starts. Uses the pinned
// Kitchensink timer API, never its private player layout. Video owns the clock;
// audio's main and decoder timers are reference-counted read-only followers.
bool create_separate_players(Kit_Source *video, int video_index, Kit_Source *audio, int audio_index,
                             const Kit_VideoFormatRequest *video_request,
                             const Kit_AudioFormatRequest *audio_request, Kit_Player *&video_player,
                             Kit_Player *&audio_player) noexcept;
} // namespace tube5
