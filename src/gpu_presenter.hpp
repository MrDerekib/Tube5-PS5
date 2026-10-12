// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <cstdint>
namespace tube5
{
bool gpu_presenter_init() noexcept;
void gpu_begin_frame() noexcept;
bool gpu_upload_nv12(unsigned char *const *planes, const int *pitches, int width, int height,
                     bool bt709, bool full_range) noexcept;
void gpu_draw_video(const SDL_Rect &area) noexcept;
void gpu_clear_video() noexcept;
bool gpu_present_ui(const std::uint32_t *pixels) noexcept;
const char *gpu_presenter_error() noexcept;
} // namespace tube5
