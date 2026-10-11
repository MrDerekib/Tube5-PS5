// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sponsor_skip.hpp"
#include <atomic>
#include <string_view>
#include <array>
namespace tube5
{
struct SponsorResponse
{
    SponsorData data{};
    double source_duration = 0;
    long http = 0, verify = -1;
    int curl_error = 0;
    std::array<char, 96> error{};
};
bool parse_sponsor_response(std::string_view json, std::string_view video_id,
                            SponsorResponse &out) noexcept;
SponsorData aligned_sponsor(const SponsorResponse &response, double duration) noexcept;
SponsorResponse fetch_sponsor(std::string_view video_id, const std::atomic<bool> &cancel) noexcept;
} // namespace tube5
