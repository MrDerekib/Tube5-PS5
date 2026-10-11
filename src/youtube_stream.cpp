// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_stream.hpp"
#include "youtube_formats.hpp"
#include "verified_https.hpp"
#include "network_source.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
extern "C"
{
#include <libavformat/avformat.h>
}
namespace tube5
{
bool youtube_content_range(const char *text, std::uint64_t wanted, std::uint64_t capacity,
                           std::uint64_t &end, std::uint64_t &total) noexcept
{
    end = total = 0;
    // A bounded header is parsed manually: no accepting negative values or
    // overflowing sscanf conversions from an untrusted HTTP response.
    if (!text || std::strncmp(text, "bytes ", 6) != 0 || !capacity)
        return false;
    const char *p = text + 6;
    const auto number = [&](std::uint64_t &value)
    {
        value = 0;
        unsigned digits = 0;
        while (*p >= '0' && *p <= '9')
        {
            if (++digits > 19 ||
                value > (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
                         (*p - '0')) /
                            10)
                return false;
            value = value * 10 + (*p++ - '0');
        }
        return digits != 0;
    };
    std::uint64_t start = 0;
    if (!number(start) || *p++ != '-' || !number(end) || *p++ != '/' || !number(total))
        return false;
    while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
        ++p;
    return !*p && start == wanted && start <= end && end < total && end - start < capacity &&
           end == std::min(total - 1, start + capacity - 1);
}
struct YouTubeStream::State
{
    static constexpr std::size_t capacity = 512 * 1024;
    std::unique_ptr<unsigned char[]> cache{new (std::nothrow) unsigned char[capacity]};
    std::array<char, 8192> url{};
    CURL *curl = nullptr;
    curl_slist *mapping = nullptr;
    Kit_Source source{};
    AVFormatContext *format = nullptr;
    AVIOContext *io = nullptr;
    std::uint64_t position = 0, cache_start = 0, total = 0;
    std::size_t cached = 0, received = 0;
    std::uint64_t expected_end = 0, response_total = 0;
    bool valid_range = false;
    std::atomic<unsigned> requests{0}, failures{0};
    std::atomic<std::uint64_t> bytes{0};
    char error[128]{};
    ~State()
    {
        avformat_close_input(&format);
        if (io)
        {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
        if (curl)
            curl_easy_cleanup(curl);
        curl_slist_free_all(mapping);
    }
    static int progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return network_session_interrupted() ? 1 : 0;
    }
    static std::size_t receive(char *data, std::size_t size, std::size_t count, void *opaque)
    {
        auto &self = *static_cast<State *>(opaque);
        if (size && count > (capacity - self.received) / size)
            return 0;
        const auto length = size * count;
        std::memcpy(self.cache.get() + self.received, data, length);
        self.received += length;
        return length;
    }
    static std::size_t header(char *data, std::size_t size, std::size_t count, void *opaque)
    {
        if (size && count > std::numeric_limits<std::size_t>::max() / size)
            return 0;
        const auto length = size * count;
        constexpr std::string_view key = "content-range:";
        bool match = length >= key.size();
        for (std::size_t i = 0; match && i < key.size(); ++i)
        {
            char c = data[i];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            match = c == key[i];
        }
        if (match)
        {
            auto &self = *static_cast<State *>(opaque);
            if (length - key.size() >= 128)
                return 0;
            char value[128]{};
            std::memcpy(value, data + key.size(), length - key.size());
            const char *p = value;
            while (*p == ' ' || *p == '\t')
                ++p;
            self.valid_range = youtube_content_range(p, self.position, capacity, self.expected_end,
                                                     self.response_total);
        }
        return length;
    }
    int refill()
    {
        if (network_session_interrupted())
            return AVERROR_EXIT;
        received = 0;
        cached = 0;
        valid_range = false;
        char range[64]{};
        std::snprintf(range, sizeof(range), "%llu-%llu", static_cast<unsigned long long>(position),
                      static_cast<unsigned long long>(position + capacity - 1));
        CURLcode code = curl_easy_setopt(curl, CURLOPT_RANGE, range);
        ++requests;
        if (code == CURLE_OK)
            code = curl_easy_perform(curl);
        long http = 0, verify = -1;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        (void)curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verify);
        if (code != CURLE_OK || http != 206 || verify || !valid_range ||
            (total && total != response_total) || received != expected_end - position + 1)
        {
            ++failures;
            std::snprintf(error, sizeof(error), "YT HTTPS: HTTP %ld CURL %d CERT %ld RANGE %d",
                          http, code, verify, valid_range);
            return code == CURLE_OPERATION_TIMEDOUT    ? AVERROR(ETIMEDOUT)
                   : code == CURLE_ABORTED_BY_CALLBACK ? AVERROR_EXIT
                                                       : AVERROR(EIO);
        }
        total = response_total;
        cache_start = position;
        cached = received;
        bytes += received;
        return 0;
    }
    static int read(void *opaque, std::uint8_t *destination, int size)
    {
        auto &self = *static_cast<State *>(opaque);
        if (size <= 0)
            return AVERROR(EINVAL);
        if (network_session_interrupted())
            return AVERROR_EXIT;
        if (self.total && self.position >= self.total)
            return AVERROR_EOF;
        if (!self.cached || self.position < self.cache_start ||
            self.position - self.cache_start >= self.cached)
        {
            const int result = self.refill();
            if (result < 0)
                return result;
        }
        const auto offset = static_cast<std::size_t>(self.position - self.cache_start);
        const auto length = std::min(static_cast<std::size_t>(size), self.cached - offset);
        std::memcpy(destination, self.cache.get() + offset, length);
        self.position += length;
        return static_cast<int>(length);
    }
    static std::int64_t seek(void *opaque, std::int64_t offset, int whence)
    {
        auto &self = *static_cast<State *>(opaque);
        if (network_session_interrupted())
            return AVERROR_EXIT;
        if (!self.total)
        {
            const int result = self.refill();
            if (result < 0)
                return result;
        }
        if (whence == AVSEEK_SIZE)
            return static_cast<std::int64_t>(self.total);
        const auto mode = whence & ~AVSEEK_FORCE;
        const std::int64_t base = mode == SEEK_SET   ? 0
                                  : mode == SEEK_CUR ? self.position
                                  : mode == SEEK_END ? self.total
                                                     : -1;
        if (base < 0 || offset < -base || offset > static_cast<std::int64_t>(self.total) - base)
            return AVERROR(EINVAL);
        self.position = static_cast<std::uint64_t>(base + offset);
        return static_cast<std::int64_t>(self.position);
    }
};
Kit_Source *YouTubeStream::open(const char *url) noexcept
{
    close();
    error_[0] = 0;
    std::array<char, 256> host{};
    if (!url || !youtube_media_host(url, host))
    {
        std::snprintf(error_, sizeof(error_), "YT: URL HTTPS NO PERMITIDA");
        return nullptr;
    }
    state_ = new (std::nothrow) State;
    if (!state_ || !state_->cache)
    {
        std::snprintf(error_, sizeof(error_), "YT: ENOMEM BUFFER HTTPS 512 KIB");
        close();
        return nullptr;
    }
    auto &self = *state_;
    std::snprintf(self.url.data(), self.url.size(), "%s", url);
    std::array<char, 16> ip{};
    int dns_error = 0;
    if (resolve_https_ipv4(host.data(), ip.data(), ip.size(), dns_error) ||
        blocked_https_ipv4(ip.data()))
        std::snprintf(error_, sizeof(error_), "YT DNS: CDN NO RESUELTO O BLOQUEADO RET 0X%08X",
                      static_cast<unsigned>(dns_error));
    else if (!network_session_interrupted())
    {
        CURLcode code = initialize_verified_https();
        self.curl = code == CURLE_OK ? curl_easy_init() : nullptr;
        if (!self.curl)
            code = CURLE_FAILED_INIT;
        char mapping[300]{};
        std::snprintf(mapping, sizeof(mapping), "%s:443:%s", host.data(), ip.data());
        self.mapping = curl_slist_append(nullptr, mapping);
        if (!self.mapping)
            code = CURLE_OUT_OF_MEMORY;
        const auto set = [&](CURLoption option, auto value)
        {
            if (code == CURLE_OK)
                code = curl_easy_setopt(self.curl, option, value);
        };
        set(CURLOPT_URL, self.url.data());
        set(CURLOPT_RESOLVE, self.mapping);
        set(CURLOPT_PROXY, "");
        set(CURLOPT_NOSIGNAL, 1L);
        set(CURLOPT_PROTOCOLS_STR, "https");
        set(CURLOPT_FOLLOWLOCATION, 0L);
        set(CURLOPT_SSL_VERIFYPEER, 1L);
        set(CURLOPT_SSL_VERIFYHOST, 2L);
        if (const char *ca = verified_https_ca())
            set(CURLOPT_CAINFO, ca);
        set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
        set(CURLOPT_USERAGENT, youtube_stream_agent);
        set(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
        set(CURLOPT_TIMEOUT_MS, 5000L);
        set(CURLOPT_WRITEFUNCTION, State::receive);
        set(CURLOPT_WRITEDATA, &self);
        set(CURLOPT_HEADERFUNCTION, State::header);
        set(CURLOPT_HEADERDATA, &self);
        set(CURLOPT_NOPROGRESS, 0L);
        set(CURLOPT_XFERINFOFUNCTION, State::progress);
        set(CURLOPT_XFERINFODATA, &self);
        if (code != CURLE_OK)
            std::snprintf(error_, sizeof(error_), "YT HTTPS INIT: CURL %d", code);
        else
        {
            self.format = avformat_alloc_context();
            auto *buffer = static_cast<unsigned char *>(av_malloc(32768));
            if (self.format && buffer)
                self.io =
                    avio_alloc_context(buffer, 32768, 0, &self, State::read, nullptr, State::seek);
            if (!self.io)
            {
                av_free(buffer);
                std::snprintf(error_, sizeof(error_), "YT: ENOMEM AVIO");
            }
            else
            {
                self.format->pb = self.io;
                self.format->flags |= AVFMT_FLAG_CUSTOM_IO;
                prepare_custom_network(self.format);
                int result =
                    avformat_open_input(&self.format, "", av_find_input_format("mov"), nullptr);
                if (result >= 0)
                    result = avformat_find_stream_info(self.format, nullptr);
                if (result >= 0 && !network_session_interrupted())
                {
                    self.source.format_ctx = self.format;
                    return &self.source;
                }
                std::snprintf(error_, sizeof(error_), "YT SOURCE %d: %.90s", result, self.error);
            }
        }
    }
    if (!error_[0])
        std::snprintf(error_, sizeof(error_), "YT: APERTURA CANCELADA");
    close();
    return nullptr;
}
void YouTubeStream::close() noexcept
{
    delete state_;
    state_ = nullptr;
}
const char *YouTubeStream::error() const noexcept
{
    return state_ && state_->error[0] ? state_->error : error_;
}
void YouTubeStream::status(char *text, std::size_t capacity) const noexcept
{
    std::snprintf(text, capacity, "YT HTTPS: REQ %u FAIL %u RX %llu KIB CACHE 512 KIB",
                  state_ ? state_->requests.load() : 0, state_ ? state_->failures.load() : 0,
                  static_cast<unsigned long long>(state_ ? state_->bytes.load() / 1024 : 0));
}
} // namespace tube5
