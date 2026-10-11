// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace tube5
{
bool youtube_visitor(std::string_view json, std::array<char, 4097> &visitor) noexcept;
}
