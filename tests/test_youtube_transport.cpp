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
namespace
{
enum class Reply
{
    valid,
    tls_error,
    wrong_range,
    missing_range,
    http_200,
    truncated,
    oversized
};
Reply reply = Reply::valid;
std::vector<char> media;
curl_write_callback writer = nullptr, header = nullptr;
void *writer_data = nullptr, *header_data = nullptr;
char range[64]{};
unsigned performs = 0;
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
    assert(info == CURLINFO_RESPONSE_CODE || info == CURLINFO_SSL_VERIFYRESULT);
    *out = info == CURLINFO_RESPONSE_CODE ? (reply == Reply::http_200 ? 200 : 206)
           : reply == Reply::tls_error    ? 62
                                          : 0;
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode __wrap_curl_easy_perform(CURL *)
{
    ++performs;
    if (reply == Reply::tls_error)
        return CURLE_PEER_FAILED_VERIFICATION;
    unsigned long long first = 0, last = 0;
    assert(std::sscanf(range, "%llu-%llu", &first, &last) == 2);
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
        std::vector<char> huge(512 * 1024 + 1);
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
    for (const auto scenario :
         {Reply::valid, Reply::tls_error, Reply::wrong_range, Reply::missing_range, Reply::http_200,
          Reply::truncated, Reply::oversized, Reply::valid})
    {
        reply = scenario;
        tube5::begin_network_session();
        auto *opened = source.open(url);
        tube5::finish_network_open();
        assert(static_cast<bool>(opened) == (reply == Reply::valid));
        if (!opened)
            assert(source.error()[0]);
        source.close();
        source.close();
    }
    tube5::begin_network_session();
    tube5::cancel_network_session();
    const auto before = performs;
    assert(!source.open(url));
    assert(performs == before);
    source.close();
}
