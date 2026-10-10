// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace tube5
{
// Opening is synchronous. Reset before each player creation so a previous
// failed attempt cannot supply a stale error for a later attempt.
void reset_codec_diagnostic() noexcept;
const char *codec_diagnostic() noexcept;
} // namespace tube5
