// SPDX-License-Identifier: GPL-3.0-or-later
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
// The native runtime has no libc++ exception/abort support. All offsets below
// are validated by the parser; construct views without throwing substr().
std::string_view slice(std::string_view text, std::size_t offset,
                       std::size_t count = std::string_view::npos) noexcept
{
    if (offset > text.size())
        return {};
    return {text.data() + offset, std::min(count, text.size() - offset)};
}
bool number(std::string_view text, unsigned limit, unsigned &out) noexcept
{
    if (text.empty())
        return false;
    unsigned value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9' || value > limit / 10)
            return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > limit)
            return false;
    }
    out = value;
    return true;
}
bool identifier(std::string_view text, unsigned max) noexcept
{
    return !text.empty() && text.size() <= max &&
           std::all_of(text.begin(), text.end(),
                       [](char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '-' || c == '_';
                       });
}
template <std::size_t N> void copy(std::array<char, N> &to, std::string_view from) noexcept
{
    std::copy(from.begin(), from.end(), to.begin());
    to[from.size()] = 0;
}
} // namespace
namespace tube5
{
bool parse_lan_catalog(std::string_view body, CatalogData &out) noexcept
{
    out = {};
    if (body.size() > 4096 || !body.starts_with("TUBE5\t1\n") || !body.ends_with('\n'))
        return false;
    CatalogData candidate{};
    body.remove_prefix(8);
    while (!body.empty())
    {
        if (candidate.count == candidate.entries.size())
            return false;
        const auto end = body.find('\n');
        auto line = slice(body, 0, end);
        body.remove_prefix(end + 1);
        std::array<std::string_view, 5> fields{};
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos)
                return false;
            fields[i] = slice(line, 0, tab);
            line.remove_prefix(tab + 1);
        }
        fields[4] = line;
        if (!identifier(fields[0], 48) || fields[1].empty() || fields[1].size() > 80 ||
            !std::all_of(fields[1].begin(), fields[1].end(),
                         [](unsigned char c) { return c >= 32 && c <= 126; }) ||
            !(fields[2] == "-" || (fields[2].size() == 11 && identifier(fields[2], 11))))
            return false;
        auto &entry = candidate.entries[candidate.count];
        if (!number(fields[3], 3600, entry.seconds) || !number(fields[4], 1080, entry.height) ||
            !(entry.height == 0 || entry.height == 360 || entry.height == 720 ||
              entry.height == 1080))
            return false;
        for (unsigned i = 0; i < candidate.count; ++i)
            if (fields[0] == candidate.entries[i].id.data())
                return false;
        copy(entry.id, fields[0]);
        copy(entry.title, fields[1]);
        if (fields[2] != "-")
            copy(entry.youtube_id, fields[2]);
        ++candidate.count;
    }
    if (!candidate.count)
        return false;
    out = candidate;
    return true;
}
bool lan_catalog_base(std::string_view source, char *out, std::size_t capacity) noexcept
{
    if (!out || !capacity)
        return false;
    out[0] = 0;
    if (!source.starts_with("http://"))
        return false;
    const auto end = source.find('/', 7);
    const auto base = slice(source, 0, end);
    auto host = slice(base, 7);
    const auto colon = host.find(':');
    if (colon != std::string_view::npos)
    {
        unsigned port = 0;
        if (!number(slice(host, colon + 1), 65535, port) || !port)
            return false;
        host = slice(host, 0, colon);
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto dot = host.find('.');
        unsigned octet = 0;
        if (!number(slice(host, 0, dot), 255, octet) ||
            (i < 3 ? dot == std::string_view::npos : dot != std::string_view::npos))
            return false;
        if (i < 3)
            host.remove_prefix(dot + 1);
    }
    if (base.size() >= capacity)
        return false;
    std::copy(base.begin(), base.end(), out);
    out[base.size()] = 0;
    return true;
}
LanCatalog::~LanCatalog()
{
    cancel();
    if (thread_)
        SDL_WaitThread(thread_, nullptr);
}
bool LanCatalog::begin(const char *source) noexcept
{
    if (loading())
        return false;
    data_ = {};
    error_ = {};
    if (!source || !lan_catalog_base(source, base_.data(), base_.size()))
    {
        std::snprintf(error_.data(), error_.size(), "CATALOGO: USA HTTP CON IP V4 NUMERICA");
        return false;
    }
    done_ = cancelled_ = false;
    deadline_ = SDL_GetTicks64() + 8000;
    result_ = 0;
    thread_ = SDL_CreateThread(worker, "tube5-catalog", this);
    if (!thread_)
    {
        std::snprintf(error_.data(), error_.size(), "CATALOGO: NO SE PUDO CREAR HILO");
        return false;
    }
    return true;
}
int LanCatalog::interrupt(void *self) noexcept
{
    const auto &catalog = *static_cast<LanCatalog *>(self);
    return catalog.cancelled_.load() || SDL_GetTicks64() >= catalog.deadline_;
}
int LanCatalog::worker(void *self) noexcept
{
    auto &catalog = *static_cast<LanCatalog *>(self);
    char url[128]{};
    std::snprintf(url, sizeof(url), "%s/catalog.tsv", catalog.base_.data());
    AVIOContext *io = nullptr;
    AVIOInterruptCB abort{interrupt, self};
    AVDictionary *options = nullptr;
    int result = av_dict_set(&options, "protocol_whitelist", "http,tcp", 0);
    if (result >= 0)
        result = av_dict_set(&options, "rw_timeout", "5000000", 0);
    if (result >= 0)
        result = avio_open2(&io, url, AVIO_FLAG_READ, &abort, &options);
    av_dict_free(&options);
    char body[4097]{};
    unsigned used = 0;
    while (result >= 0 && used < sizeof(body))
    {
        const int read =
            avio_read(io, reinterpret_cast<unsigned char *>(body + used), sizeof(body) - used);
        if (read == AVERROR_EOF)
            break;
        if (read <= 0)
        {
            result = read ? read : AVERROR(EIO);
            break;
        }
        used += static_cast<unsigned>(read);
    }
    if (io)
        avio_closep(&io);
    if (result >= 0 && !parse_lan_catalog(std::string_view{body, used}, catalog.data_))
        result = AVERROR_INVALIDDATA;
    catalog.result_ = result;
    catalog.done_ = true;
    return 0;
}
void LanCatalog::poll() noexcept
{
    if (!loading() || !done_.load())
        return;
    SDL_WaitThread(thread_, nullptr);
    thread_ = nullptr;
    if (cancelled_.load())
        data_ = {};
    else if (result_ < 0)
    {
        data_ = {};
        char detail[80]{};
        av_strerror(result_, detail, sizeof(detail));
        std::snprintf(error_.data(), error_.size(), "CATALOGO: %s", detail);
    }
}
bool LanCatalog::media_url(unsigned entry, char *out, std::size_t capacity) const noexcept
{
    if (!out || !capacity || loading() || entry >= data_.count)
        return false;
    const int size = std::snprintf(out, capacity, "%s/media/%s.mp4", base_.data(),
                                   data_.entries[entry].id.data());
    return size >= 0 && static_cast<std::size_t>(size) < capacity;
}
} // namespace tube5
