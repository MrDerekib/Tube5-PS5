// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <kitchensink2/kitsource.h>
namespace tube5
{
struct YouTubeTransferTiming
{
    std::uint64_t completed = 0, elapsed_ms = 0, bytes = 0;
};
// Pure range validation shared with the deterministic transport tests.
bool youtube_content_range(const char *text, std::uint64_t wanted, std::uint64_t capacity,
                           std::uint64_t &end, std::uint64_t &total) noexcept;
class YouTubeStream final
{
  public:
    ~YouTubeStream()
    {
        close();
    }
    YouTubeStream() = default;
    YouTubeStream(const YouTubeStream &) = delete;
    YouTubeStream &operator=(const YouTubeStream &) = delete;
    Kit_Source *open(const char *url, bool larger_sequential_ranges = false) noexcept;
    void close() noexcept; // Only after the decoder/demux threads have joined.
    // Runtime error is read only after the demux failure has been published.
    const char *error() const noexcept;
    void status(char *text, std::size_t capacity) const noexcept;
    // Numeric-only diagnostics, retained after teardown until the next open.
    void diagnostic(unsigned row, char *text, std::size_t capacity) const noexcept;
    YouTubeTransferTiming transfer_timing() const noexcept;

  private:
    struct State;
    State *state_ = nullptr;
    char error_[128]{};
    char retained_[3][192]{};
};
} // namespace tube5
