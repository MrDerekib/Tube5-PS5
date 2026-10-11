// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <string_view>
#include <SDL.h>
namespace tube5
{
enum class SearchState
{
    idle,
    searching,
    ready,
    preparing,
    failed
};
struct SearchEntry
{
    std::array<char, 12> id{};
    std::array<char, 81> title{};
    std::array<char, 49> channel{};
    unsigned seconds = 0;
};
struct SearchData
{
    SearchState state = SearchState::idle;
    std::array<SearchEntry, 8> entries{};
    unsigned count = 0;
    std::array<char, 49> prepared_id{};
};
bool parse_search_reply(std::string_view body, SearchData &out) noexcept;
class SearchKeyboard
{
  public:
    static constexpr std::string_view keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-_";
    void move(int delta) noexcept
    {
        selected_ = (selected_ + delta % 40 + 40) % 40;
    }
    void choose() noexcept
    {
        if (used_ < 48)
        {
            query_[used_++] = keys[selected_];
            query_[used_] = 0;
        }
    }
    void erase() noexcept
    {
        if (used_)
            query_[--used_] = 0;
    }
    const char *query() const noexcept
    {
        return query_.data();
    }
    unsigned selected() const noexcept
    {
        return selected_;
    }

  private:
    std::array<char, 49> query_{};
    unsigned used_ = 0;
    int selected_ = 0;
};
// Replaceable transport boundary: the UI only consumes bounded results/status.
class YouTubeSearch
{
  public:
    enum class Request
    {
        search,
        prepare,
        job
    };
    ~YouTubeSearch();
    bool begin(const char *source, const char *token, Request request,
               const char *value = "") noexcept;
    void cancel() noexcept
    {
        cancelled_ = true;
    }
    void poll() noexcept;
    bool loading() const noexcept
    {
        return thread_ != nullptr;
    }
    bool cancelled() const noexcept
    {
        return cancelled_.load();
    }
    const SearchData &data() const noexcept
    {
        return data_;
    }
    const char *error() const noexcept
    {
        return error_.data();
    }

  private:
    static int worker(void *self) noexcept;
    static int interrupt(void *self) noexcept;
    SDL_Thread *thread_ = nullptr;
    std::atomic<bool> done_{false}, cancelled_{false};
    Uint64 deadline_ = 0;
    Request request_ = Request::search;
    std::array<char, 320> url_{};
    std::array<char, 128> headers_{};
    std::array<char, 96> error_{};
    SearchData data_{};
    SearchData reply_{};
    int result_ = 0;
};
} // namespace tube5
