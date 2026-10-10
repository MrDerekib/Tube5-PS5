// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace tube5
{
// PacBrew's PS5 video driver initializes keyboard input even without a USB
// keyboard attached. A native title must load that sysmodule before SDL.
int prepare_native_sdl() noexcept;
} // namespace tube5
