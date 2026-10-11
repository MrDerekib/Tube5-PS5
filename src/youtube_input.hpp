// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <string_view>
namespace tube5
{
inline constexpr std::string_view youtube_test_id = "bHIhgxav9LY";
// Avoid string_view::substr: the native SDK has no exception runtime.
inline std::string_view youtube_before(std::string_view text, std::size_t offset) noexcept
{
    return {text.data(), std::min(offset, text.size())};
}
inline std::string_view youtube_after(std::string_view text, std::size_t offset) noexcept
{
    return text.empty() || offset > text.size()
               ? std::string_view{}
               : std::string_view{text.data() + offset, text.size() - offset};
}
inline bool valid_youtube_id(std::string_view id) noexcept
{
    if (id.size() != 11)
        return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    return true;
}
// Extract identity only. Never connect to the submitted URL or follow redirects.
inline bool youtube_input_id(std::string_view input, std::array<char, 12> &out) noexcept
{
    out = {};
    while (!input.empty() && input.front() == ' ')
        input.remove_prefix(1);
    while (!input.empty() && input.back() == ' ')
        input.remove_suffix(1);
    if (input.empty() || input.size() > 1024)
        return false;
    for (unsigned char c : input)
        if (c <= 32 || c >= 127 || c == '\\')
            return false;
    std::string_view id = input;
    if (!valid_youtube_id(id))
    {
        if (input.starts_with("https://"))
            input.remove_prefix(8);
        else if (input.starts_with("http://"))
            input.remove_prefix(7);
        else
            return false;
        const auto slash = input.find('/');
        if (slash == std::string_view::npos)
            return false;
        const auto host = youtube_before(input, slash);
        auto path = youtube_after(input, slash);
        path = youtube_before(path, path.find('#'));
        const auto query_pos = path.find('?');
        auto query = query_pos == std::string_view::npos ? std::string_view{}
                                                         : youtube_after(path, query_pos + 1);
        path = youtube_before(path, query_pos);
        if (host == "youtu.be")
            id = youtube_after(path, 1);
        else if (host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com" ||
                 host == "music.youtube.com")
        {
            if (path == "/watch")
            {
                id = {};
                bool found = false;
                while (!query.empty())
                {
                    const auto end = query.find('&');
                    const auto pair = youtube_before(query, end);
                    if (pair.starts_with("v="))
                    {
                        if (found)
                            return false;
                        found = true;
                        id = youtube_after(pair, 2);
                    }
                    if (end == std::string_view::npos)
                        break;
                    query.remove_prefix(end + 1);
                }
            }
            else if (path.starts_with("/shorts/"))
                id = youtube_after(path, 8);
            else if (path.starts_with("/live/"))
                id = youtube_after(path, 6);
            else if (path.starts_with("/embed/"))
                id = youtube_after(path, 7);
            else
                return false;
        }
        else
            return false;
    }
    if (!valid_youtube_id(id))
        return false;
    std::copy(id.begin(), id.end(), out.begin());
    return true;
}
class VideoKeyboard
{
  public:
    static constexpr std::string_view keys =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_:/?=&.% ";
    void move(int delta) noexcept
    {
        constexpr int count = keys.size();
        selected_ = (selected_ + delta % count + count) % count;
    }
    void choose() noexcept
    {
        append(keys[selected_]);
    }
    void append(char c) noexcept
    {
        if (used_ < input_.size() - 1 && keys.find(c) != std::string_view::npos)
        {
            input_[used_++] = c;
            input_[used_] = 0;
        }
    }
    void erase() noexcept
    {
        if (used_)
            input_[--used_] = 0;
    }
    void clear() noexcept
    {
        input_ = {};
        used_ = 0;
    }
    bool set(std::string_view text) noexcept
    {
        if (text.size() >= input_.size())
            return false;
        for (char c : text)
            if (keys.find(c) == std::string_view::npos)
                return false;
        clear();
        for (char c : text)
            append(c);
        return true;
    }
    const char *input() const noexcept
    {
        return input_.data();
    }
    unsigned selected() const noexcept
    {
        return selected_;
    }

  private:
    std::array<char, 1025> input_{};
    unsigned used_ = 0;
    int selected_ = 0;
};
} // namespace tube5
