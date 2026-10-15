// SPDX-License-Identifier: GPL-3.0-or-later
// Offline curl/DNS test doubles: no requests leave this process.
#include "youtube_stream.hpp"
#include "network_source.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <arpa/inet.h>
#include <netdb.h>
#include <curl/curl.h>
extern "C"
{
#include <libavformat/avformat.h>
}
namespace
{
enum class Reply
{
    valid,
    tls_error,
    wrong_range,
    missing_range,
    http_200,
    http_403,
    truncated,
    oversized
};
Reply reply = Reply::valid;
std::vector<char> media;
curl_write_callback writer = nullptr, header = nullptr;
void *writer_data = nullptr, *header_data = nullptr;
char range[64]{};
unsigned performs = 0;
std::vector<std::pair<unsigned long long, unsigned long long>> requested_ranges;
} // namespace
extern "C" int __real_getaddrinfo(const char *, const char *, const addrinfo *, addrinfo **out)
{
    auto *entry = static_cast<addrinfo *>(std::calloc(1, sizeof(addrinfo) + sizeof(sockaddr_in)));
    assert(entry);
    auto *in = reinterpret_cast<sockaddr_in *>(entry + 1);
    in->sin_family = AF_INET;
    assert(inet_pton(AF_INET, "142.251.152.4", &in->sin_addr) == 1);
    entry->ai_family = AF_INET;
    entry->ai_addr = reinterpret_cast<sockaddr *>(in);
    entry->ai_addrlen = sizeof(*in);
    *out = entry;
    return 0;
}
extern "C" void __real_freeaddrinfo(addrinfo *entry)
{
    std::free(entry);
}
extern "C" CURLcode __wrap_curl_easy_setopt(CURL *, int requested, ...)
{
    va_list args;
    va_start(args, requested);
    const auto option = static_cast<CURLoption>(requested);
    switch (option)
    {
    case CURLOPT_WRITEFUNCTION:
        writer = va_arg(args, curl_write_callback);
        break;
    case CURLOPT_HEADERFUNCTION:
        header = va_arg(args, curl_write_callback);
        break;
    case CURLOPT_WRITEDATA:
        writer_data = va_arg(args, void *);
        break;
    case CURLOPT_HEADERDATA:
        header_data = va_arg(args, void *);
        break;
    case CURLOPT_RANGE:
        std::snprintf(range, sizeof(range), "%s", va_arg(args, const char *));
        break;
    case CURLOPT_SSL_VERIFYPEER:
        assert(va_arg(args, long) == 1);
        break;
    case CURLOPT_SSL_VERIFYHOST:
        assert(va_arg(args, long) == 2);
        break;
    case CURLOPT_FOLLOWLOCATION:
        assert(va_arg(args, long) == 0);
        break;
    case CURLOPT_PROTOCOLS_STR:
        assert(std::strcmp(va_arg(args, const char *), "https") == 0);
        break;
    case CURLOPT_PROXY:
        assert(!*va_arg(args, const char *));
        break;
    default:
        break; // Test double performs no network setup.
    }
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode __wrap_curl_easy_getinfo(CURL *, int requested, ...)
{
    va_list args;
    va_start(args, requested);
    const auto info = static_cast<CURLINFO>(requested);
    auto *out = va_arg(args, long *);
    assert(info == CURLINFO_RESPONSE_CODE || info == CURLINFO_SSL_VERIFYRESULT ||
           info == CURLINFO_NUM_CONNECTS);
    *out = info == CURLINFO_NUM_CONNECTS    ? 1
           : info == CURLINFO_RESPONSE_CODE ? (reply == Reply::http_200   ? 200
                                               : reply == Reply::http_403 ? 403
                                                                          : 206)
           : reply == Reply::tls_error      ? 62
                                            : 0;
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode __wrap_curl_easy_perform(CURL *)
{
    ++performs;
    if (reply == Reply::tls_error)
        return CURLE_PEER_FAILED_VERIFICATION;
    if (reply == Reply::http_403)
        return CURLE_OK;
    unsigned long long first = 0, last = 0;
    assert(std::sscanf(range, "%llu-%llu", &first, &last) == 2);
    requested_ranges.emplace_back(first, last);
    assert(first < media.size());
    last = std::min(last, static_cast<unsigned long long>(media.size() - 1));
    if (reply != Reply::missing_range)
    {
        char line[128]{};
        std::snprintf(line, sizeof(line), "Content-Range: bytes %llu-%llu/%zu\r\n",
                      first + (reply == Reply::wrong_range), last, media.size());
        assert(header(line, 1, std::strlen(line), header_data) == std::strlen(line));
    }
    std::size_t length = static_cast<std::size_t>(last - first + 1);
    if (reply == Reply::truncated)
        length /= 2;
    if (reply == Reply::oversized)
    {
        std::vector<char> huge(static_cast<std::size_t>(last - first + 2));
        return writer(huge.data(), 1, huge.size(), writer_data) ? CURLE_OK : CURLE_WRITE_ERROR;
    }
    return writer(media.data() + first, 1, length, writer_data) == length ? CURLE_OK
                                                                          : CURLE_WRITE_ERROR;
}
int main()
{
    std::ifstream file("assets/test-clip.mp4", std::ios::binary);
    assert(file);
    media.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    assert(!media.empty());
    tube5::YouTubeStream source;
    constexpr const char *url = "https://rr1.googlevideo.com/videoplayback?fixture=1";
    for (const bool larger : {false, true})
    for (const auto scenario :
         {Reply::valid, Reply::tls_error, Reply::wrong_range, Reply::missing_range, Reply::http_200,
          Reply::http_403, Reply::truncated, Reply::oversized, Reply::valid})
    {
        reply = scenario;
        tube5::begin_network_session();
        auto *opened = source.open(url, larger);
        tube5::finish_network_open();
        assert(static_cast<bool>(opened) == (reply == Reply::valid));
        if (!opened)
            assert(source.error()[0]);
        source.close();
        source.close();
        char diagnostic[192]{};
        source.diagnostic(0, diagnostic, sizeof(diagnostic));
        if (scenario == Reply::http_403)
        {
            assert(std::strstr(diagnostic, "HTTP 403 CURL 0 CERT 0 RANGE 0"));
            assert(std::strstr(diagnostic, "REQ 1 OK 0 FAIL "));
        }
        if (scenario == Reply::valid)
            assert(std::strstr(diagnostic, "FAIL 0 HTTP 206"));
        for (unsigned row = 0; row < 3; ++row)
        {
            source.diagnostic(row, diagnostic, sizeof(diagnostic));
            assert(!std::strstr(diagnostic, "googlevideo"));
            assert(!std::strstr(diagnostic, "fixture"));
        }
        source.diagnostic(9, diagnostic, sizeof(diagnostic));
        assert(!*diagnostic);
    }
    // Failure while reading an already-open source, not just startup. Keep
    // the first 403 and the preceding good range even if FFmpeg retries.
    reply = Reply::valid;
    tube5::begin_network_session();
    auto *opened = source.open(url);
    tube5::finish_network_open();
    assert(opened);
    auto *io = static_cast<AVFormatContext *>(opened->format_ctx)->pb;
    std::uint8_t sample[16]{};
    assert(io->seek(io->opaque, 0, SEEK_SET) == 0);
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) == sizeof(sample));
    assert(io->seek(io->opaque, 1048576, SEEK_SET) == 1048576);
    reply = Reply::http_403;
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) < 0);
    reply = Reply::http_200;
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) < 0);
    source.close();
    source.close();
    char diagnostic[192]{};
    source.diagnostic(0, diagnostic, sizeof(diagnostic));
    assert(std::strstr(diagnostic, "HTTP 403 CURL 0 CERT 0 RANGE 0"));
    source.diagnostic(1, diagnostic, sizeof(diagnostic));
    assert(std::strstr(diagnostic, "BYTES 1048576-1572863 RX 0"));
    assert(std::strstr(diagnostic, "NEWCONN 1"));
    source.diagnostic(2, diagnostic, sizeof(diagnostic));
    assert(std::strstr(diagnostic, "LAST OK 0-524287"));
    source.diagnostic(0, nullptr, 0);
    // Larger 4K sequential window: no startup/first-seek growth, exact payload,
    // 1 MiB only after consuming the first 512 KiB, then backward seek small.
    media.resize(std::max<std::size_t>(media.size(), 8 * 1024 * 1024), 0);
    reply = Reply::valid;
    tube5::begin_network_session();
    requested_ranges.clear();
    opened = source.open(url, true);
    tube5::finish_network_open();
    assert(opened && !requested_ranges.empty());
    assert(requested_ranges[0].second - requested_ranges[0].first + 1 == 512 * 1024);
    io = static_cast<AVFormatContext *>(opened->format_ctx)->pb;
    requested_ranges.clear();
    std::uint64_t cursor = 4 * 1024 * 1024;
    assert(io->seek(io->opaque, cursor, SEEK_SET) == static_cast<std::int64_t>(cursor));
    const auto consume = [&](std::size_t remaining)
    {
        std::uint8_t block[32768];
        while (remaining)
        {
            const int amount = io->read_packet(io->opaque, block,
                                              std::min<std::size_t>(remaining, sizeof(block)));
            assert(amount > 0 && static_cast<std::size_t>(amount) <= remaining);
            assert(!std::memcmp(block, media.data() + cursor, amount));
            remaining -= amount;
            cursor += amount;
        }
    };
    consume(512 * 1024);
    assert(requested_ranges.size() == 1);
    assert(requested_ranges.back().second - requested_ranges.back().first + 1 == 512 * 1024);
    // A no-op position query/seek at the boundary keeps sequential growth.
    assert(io->seek(io->opaque, 0, SEEK_CUR) == static_cast<std::int64_t>(cursor));
    consume(1024 * 1024);
    assert(requested_ranges.size() == 2);
    assert(requested_ranges.back().second - requested_ranges.back().first + 1 == 1024 * 1024);
    consume(1024 * 1024);
    assert(requested_ranges.size() == 3);
    assert(requested_ranges.back().second - requested_ranges.back().first + 1 == 1024 * 1024);
    cursor = 3 * 1024 * 1024;
    assert(io->seek(io->opaque, cursor, SEEK_SET) == static_cast<std::int64_t>(cursor));
    consume(16);
    assert(requested_ranges.size() == 4);
    assert(requested_ranges.back().second - requested_ranges.back().first + 1 == 512 * 1024);
    // Range growth must not weaken short/oversized response validation.
    consume(512 * 1024 - 16);
    reply = Reply::truncated;
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) < 0);
    reply = Reply::oversized;
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) < 0);
    reply = Reply::valid;
    cursor = media.size() - 16;
    assert(io->seek(io->opaque, cursor, SEEK_SET) == static_cast<std::int64_t>(cursor));
    consume(16);
    const auto at_eof = performs;
    assert(io->read_packet(io->opaque, sample, sizeof(sample)) == AVERROR_EOF);
    assert(performs == at_eof);
    assert(source.transfer_timing().completed > 0 && source.transfer_timing().bytes > 0);
    source.close();
    assert(!source.transfer_timing().completed);
    tube5::begin_network_session();
    tube5::cancel_network_session();
    const auto before = performs;
    assert(!source.open(url));
    assert(performs == before);
    source.close();
    source.diagnostic(0, diagnostic, sizeof(diagnostic));
    assert(std::strstr(diagnostic, "REQ 0 OK 0 FAIL 0"));
    assert(!std::strstr(diagnostic, "HTTP 403"));
    std::puts("YouTube ranges: initial/seek 512 KiB, sequential 1 MiB, payload, EOF, TLS/range errors and cancellation passed");
}
