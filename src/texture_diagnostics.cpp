// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_diagnostics.hpp"

#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#include <SDL.h>

extern "C" SDL_Surface *__real_SDL_CreateRGBSurface_REAL(Uint32, int, int, int, Uint32, Uint32,
                                                         Uint32, Uint32);

// PacBrew's SW_CreateTexture calls this constructor, then unconditionally calls
// surface setters. On failure those setters overwrite the useful original error
// with "Parameter 'surface' is invalid". Report the error BEFORE that happens.
extern "C" SDL_Surface *__wrap_SDL_CreateRGBSurface_REAL(Uint32 flags, int width, int height,
                                                         int depth, Uint32 red, Uint32 green,
                                                         Uint32 blue, Uint32 alpha)
{
    SDL_Surface *surface =
        __real_SDL_CreateRGBSurface_REAL(flags, width, height, depth, red, green, blue, alpha);
    if (!surface)
    {
        tube5_boot_checkpoint("Tube5 TEXTURE: fallo original de superficie");
        tube5_boot_checkpoint(SDL_GetError());
        tube5_boot_result("Tube5 TEXTURE: ancho", static_cast<Uint32>(width));
        tube5_boot_result("Tube5 TEXTURE: alto", static_cast<Uint32>(height));
        tube5_boot_result("Tube5 TEXTURE: bits", static_cast<Uint32>(depth));
    }
    return surface;
}
#endif
