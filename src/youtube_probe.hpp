// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <array>
#include <atomic>
#include <string_view>
#include <curl/curl.h>
namespace tube5
{
enum class ProbeStage
{
    idle,
    dns,
    https,
    ready,
    failed,
    cancelled
};
struct ProbeResult
{
    std::array<char, 16> ip{};
    std::array<char, 160> ca{};
    std::array<char, 128> title{};
    std::array<char, 96> error{};
    unsigned bytes = 0;
    int dns_error = 0, curl_error = 0;
    long http = 0, verify = -1;
};
// Bounded JSON string reader for the oEmbed diagnostic, not a YouTube resolver.
bool probe_title(std::string_view json, std::array<char, 128> &title) noexcept;
class YouTubeProbe
{
  public:
    ~YouTubeProbe();
    bool begin() noexcept;
    void cancel() noexcept
    {
        cancelled_ = true;
    }
    void poll() noexcept;
    bool loading() const noexcept
    {
        return thread_ != nullptr;
    }
    ProbeStage stage() const noexcept
    {
        return stage_.load();
    }
    const ProbeResult &result() const noexcept
    {
        return result_;
    }

  private:
    static int worker(void *) noexcept;
    static int progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept;
    SDL_Thread *thread_ = nullptr;
    std::atomic<bool> done_{false}, cancelled_{false};
    std::atomic<ProbeStage> stage_{ProbeStage::idle};
    ProbeResult result_{}, pending_{};
};
} // namespace tube5
