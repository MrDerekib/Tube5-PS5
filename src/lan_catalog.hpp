// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <string_view>
#include <SDL.h>
#include "sponsor_skip.hpp"
namespace tube5
{
struct CatalogEntry
{
    std::array<char, 49> id{};
    std::array<char, 81> title{};
    std::array<char, 12> youtube_id{};
    unsigned seconds = 0, height = 0;
    SponsorData sponsor{};
};
struct CatalogData
{
    std::array<CatalogEntry, 8> entries{};
    unsigned count = 0;
};
bool parse_lan_catalog(std::string_view body, CatalogData &out) noexcept;
bool lan_catalog_base(std::string_view source, char *out, std::size_t capacity) noexcept;

class LanCatalog final
{
  public:
    ~LanCatalog();
    bool begin(const char *source) noexcept;
    void cancel() noexcept
    {
        cancelled_ = true;
    }
    void poll() noexcept;
    bool loading() const noexcept
    {
        return thread_ != nullptr;
    }
    bool cancelling() const noexcept
    {
        return cancelled_.load();
    }
    const CatalogData &data() const noexcept
    {
        return data_;
    }
    const char *error() const noexcept
    {
        return error_.data();
    }
    bool media_url(unsigned entry, char *out, std::size_t capacity) const noexcept;

  private:
    static int worker(void *self) noexcept;
    static int interrupt(void *self) noexcept;
    SDL_Thread *thread_ = nullptr;
    std::atomic<bool> done_{false}, cancelled_{false};
    Uint64 deadline_ = 0;
    int result_ = 0;
    CatalogData data_{};
    std::array<char, 64> base_{};
    std::array<char, 128> error_{};
};
} // namespace tube5
