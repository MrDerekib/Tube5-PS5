// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_search.hpp"
#include "lan_catalog.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C"
{
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
}
namespace
{
bool identifier(std::string_view text, unsigned length) noexcept
{
    return text.size() == length &&
           std::all_of(text.begin(), text.end(),
                       [](unsigned char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '-' || c == '_';
                       });
}
template <std::size_t N> bool copy(std::array<char, N> &out, std::string_view text) noexcept
{
    if (text.empty() || text.size() >= N ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
        return false;
    std::copy(text.begin(), text.end(), out.begin());
    out[text.size()] = 0;
    return true;
}
bool state(std::string_view value, tube5::SearchState &out) noexcept
{
    if (value == "idle")
        out = tube5::SearchState::idle;
    else if (value == "searching")
        out = tube5::SearchState::searching;
    else if (value == "ready")
        out = tube5::SearchState::ready;
    else if (value == "preparing")
        out = tube5::SearchState::preparing;
    else if (value == "failed")
        out = tube5::SearchState::failed;
    else
        return false;
    return true;
}
} // namespace
namespace tube5
{
bool parse_search_reply(std::string_view body, SearchData &out) noexcept
{
    out = {};
    if (body.size() > 2048 || !body.ends_with('\n'))
        return false;
    SearchData candidate{};
    if (body.starts_with("TUBE5_JOB\t1\t"))
    {
        body.remove_prefix(12);
        const auto tab = body.find('\t');
        if (tab == std::string_view::npos || !state({body.data(), tab}, candidate.state) ||
            candidate.state == SearchState::searching)
            return false;
        body.remove_prefix(tab + 1);
        body.remove_suffix(1);
        if (body != "-")
        {
            if (body.size() != 21 || !body.starts_with("yt-") || !body.ends_with("-360-60") ||
                !identifier({body.data() + 3, 11}, 11) || !copy(candidate.prepared_id, body))
                return false;
        }
        if ((candidate.state == SearchState::ready || candidate.state == SearchState::preparing) &&
            !candidate.prepared_id[0])
            return false;
        out = candidate;
        return true;
    }
    if (!body.starts_with("TUBE5_SEARCH\t1\t"))
        return false;
    body.remove_prefix(15);
    const auto end = body.find('\n');
    if (!state({body.data(), end}, candidate.state) || candidate.state == SearchState::preparing)
        return false;
    body.remove_prefix(end + 1);
    if (candidate.state != SearchState::ready && !body.empty())
        return false;
    while (!body.empty())
    {
        if (candidate.count == candidate.entries.size())
            return false;
        const auto newline = body.find('\n');
        std::string_view line{body.data(), newline};
        body.remove_prefix(newline + 1);
        std::array<std::string_view, 4> fields{};
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos)
                return false;
            fields[i] = {line.data(), tab};
            line.remove_prefix(tab + 1);
        }
        fields[3] = line;
        auto &entry = candidate.entries[candidate.count];
        if (!identifier(fields[0], 11) || !copy(entry.id, fields[0]) ||
            !copy(entry.title, fields[1]) || !copy(entry.channel, fields[2]) || fields[3].empty())
            return false;
        for (char c : fields[3])
        {
            if (c < '0' || c > '9' || entry.seconds > 360)
                return false;
            entry.seconds = entry.seconds * 10 + static_cast<unsigned>(c - '0');
        }
        if (entry.seconds < 20 || entry.seconds > 3600)
            return false;
        for (unsigned i = 0; i < candidate.count; ++i)
            if (std::strcmp(candidate.entries[i].id.data(), entry.id.data()) == 0)
                return false;
        ++candidate.count;
    }
    out = candidate;
    return true;
}
YouTubeSearch::~YouTubeSearch()
{
    cancel();
    if (thread_)
        SDL_WaitThread(thread_, nullptr);
}
bool YouTubeSearch::begin(const char *source, const char *token, Request request,
                          const char *value) noexcept
{
    if (loading())
        return false;
    error_ = {};
    result_ = 0;
    char base[96]{};
    if (!source || !lan_catalog_base(source, base, sizeof(base)) || !token ||
        std::strlen(token) != 64 ||
        !std::all_of(token, token + 64,
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
    {
        std::snprintf(error_.data(), error_.size(), "BUSQUEDA: FALTA CLAVE O IP DEL PUENTE");
        return false;
    }
    if (!value)
        return false;
    if (request == Request::prepare)
    {
        if (!identifier(value, 11))
            return false;
        std::snprintf(url_.data(), url_.size(), "%s/tube5/prepare?id=%s", base, value);
    }
    else if (request == Request::job)
        std::snprintf(url_.data(), url_.size(), "%s/tube5/job", base);
    else
    {
        const auto length = std::strlen(value);
        if (!length || length > 48)
            return false;
        char encoded[145]{};
        unsigned used = 0;
        bool nonspace = false;
        for (unsigned i = 0; i < length; ++i)
        {
            const char c = value[i];
            if (SearchKeyboard::keys.find(c) == std::string_view::npos)
                return false;
            if (c == ' ')
            {
                encoded[used++] = '%';
                encoded[used++] = '2';
                encoded[used++] = '0';
            }
            else
            {
                encoded[used++] = c;
                nonspace = true;
            }
        }
        if (!nonspace)
            return false;
        std::snprintf(url_.data(), url_.size(), "%s/tube5/search?q=%s", base, encoded);
    }
    std::snprintf(headers_.data(), headers_.size(), "Authorization: Bearer %s\r\n%s", token,
                  request == Request::prepare ? "Content-Length: 0\r\n" : "");
    request_ = request;
    done_ = cancelled_ = false;
    deadline_ = SDL_GetTicks64() + 8000;
    thread_ = SDL_CreateThread(worker, "tube5-search", this);
    if (!thread_)
        std::snprintf(error_.data(), error_.size(), "BUSQUEDA: NO SE PUDO CREAR HILO");
    return thread_ != nullptr;
}
int YouTubeSearch::interrupt(void *self) noexcept
{
    const auto &search = *static_cast<YouTubeSearch *>(self);
    return search.cancelled_.load() || SDL_GetTicks64() >= search.deadline_;
}
int YouTubeSearch::worker(void *self) noexcept
{
    auto &search = *static_cast<YouTubeSearch *>(self);
    AVIOContext *io = nullptr;
    AVDictionary *options = nullptr;
    AVIOInterruptCB abort{interrupt, self};
    int result = av_dict_set(&options, "protocol_whitelist", "http,tcp", 0);
    if (result >= 0)
        result = av_dict_set(&options, "rw_timeout", "5000000", 0);
    if (result >= 0)
        result = av_dict_set(&options, "headers", search.headers_.data(), 0);
    if (result >= 0)
        result = av_dict_set(&options, "method",
                             search.request_ == Request::prepare ? "POST" : "GET", 0);
    if (result >= 0)
        result = av_dict_set(&options, "chunked_post", "0", 0);
    if (result >= 0)
        result = avio_open2(&io, search.url_.data(), AVIO_FLAG_READ, &abort, &options);
    av_dict_free(&options);
    char body[2049]{};
    unsigned used = 0;
    while (result >= 0 && used < sizeof(body))
    {
        const int count =
            avio_read(io, reinterpret_cast<unsigned char *>(body + used), sizeof(body) - used);
        if (count == AVERROR_EOF)
            break;
        if (count <= 0)
        {
            result = count ? count : AVERROR(EIO);
            break;
        }
        used += static_cast<unsigned>(count);
    }
    if (io)
        avio_closep(&io);
    SearchData reply{};
    const std::string_view response{body, used};
    const bool matching = search.request_ == Request::search
                              ? response.starts_with("TUBE5_SEARCH\t")
                              : response.starts_with("TUBE5_JOB\t");
    if (result >= 0 && (!matching || !parse_search_reply(response, reply)))
        result = AVERROR_INVALIDDATA;
    if (result >= 0)
        search.reply_ = reply;
    search.result_ = result;
    search.done_ = true;
    return 0;
}
void YouTubeSearch::poll() noexcept
{
    if (!thread_ || !done_.load())
        return;
    SDL_WaitThread(thread_, nullptr);
    thread_ = nullptr;
    if (cancelled_.load())
        return;
    if (result_ >= 0)
        data_ = reply_;
    if (result_ < 0)
    {
        data_.state = SearchState::failed;
        char detail[64]{};
        av_strerror(result_, detail, sizeof(detail));
        std::snprintf(error_.data(), error_.size(), "BUSQUEDA: %s", detail);
    }
}
} // namespace tube5
