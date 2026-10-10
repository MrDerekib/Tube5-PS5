// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <cstddef>
namespace tube5
{
SDL_Surface *create_texture_surface(Uint32 flags, int width, int height, int depth, Uint32 red,
                                    Uint32 green, Uint32 blue, Uint32 alpha) noexcept;
void native_surface_buffer_status(char *text, std::size_t capacity) noexcept;
} // namespace tube5
