// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_probe.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <pthread.h>
#ifdef TUBE5_HOST
#include <arpa/inet.h>
#include <netdb.h>
#ifdef TUBE5_NETWORK_WRAP
extern "C" int __real_getaddrinfo(const char *, const char *, const struct addrinfo *,
                                  struct addrinfo **);
extern "C" void __real_freeaddrinfo(struct addrinfo *);
#endif
#else
extern "C"
{
    int tube5_resolve_ipv4(const char *, char *, std::size_t, int *);
    const char *console_curl_ca_file(void);
}
#endif
namespace
{
constexpr const char *host = "www.youtube.com";
constexpr const char *url =
    "https://www.youtube.com/"
    "oembed?url=https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3DbHIhgxav9LY&format=json";
pthread_once_t curl_once = PTHREAD_ONCE_INIT;
CURLcode curl_initialized = CURLE_FAILED_INIT;
void initialize_curl()
{
    curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
}
struct Body
{
    std::array<char, 16384> bytes{};
    std::size_t used = 0;
};
std::size_t receive(char *data, std::size_t size, std::size_t count, void *opaque)
{
    auto &body = *static_cast<Body *>(opaque);
    if (size && count > (body.bytes.size() - body.used) / size)
        return 0;
    const auto bytes = size * count;
    std::copy_n(data, bytes, body.bytes.data() + body.used);
    body.used += bytes;
    return bytes;
}
} // namespace
namespace tube5
{
bool probe_title(std::string_view json, std::array<char, 128> &title) noexcept
{
    title = {};
    if (json.size() > 16384 || json.find("\"author_name\"") == std::string_view::npos)
        return false;
    auto offset = json.find("\"title\"");
    if (offset == std::string_view::npos)
        return false;
    offset += 7;
    const auto skip = [&]
    {
        while (offset < json.size() && (json[offset] == ' ' || json[offset] == '\t' ||
                                        json[offset] == '\n' || json[offset] == '\r'))
            ++offset;
    };
    skip();
    if (offset == json.size() || json[offset++] != ':')
        return false;
    skip();
    if (offset == json.size() || json[offset++] != '"')
        return false;
    unsigned used = 0;
    while (offset < json.size())
    {
        unsigned char c = json[offset++];
        if (c == '"')
            return used > 0;
        if (c < 32)
            return false;
        if (c == '\\')
        {
            if (offset == json.size())
                return false;
            c = json[offset++];
            if (c == 'u')
            {
                if (json.size() - offset < 4)
                    return false;
                for (unsigned i = 0; i < 4; ++i)
                {
                    const char h = json[offset++];
                    if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') ||
                          (h >= 'A' && h <= 'F')))
                        return false;
                }
                c = '?';
            }
            else if (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f')
                c = ' ';
            else if (c != '"' && c != '\\' && c != '/')
                return false;
        }
        if (used < title.size() - 1)
            title[used++] = c < 127 ? static_cast<char>(c) : '?';
    }
    title = {};
    return false;
}
YouTubeProbe::~YouTubeProbe()
{
    cancel();
    if (thread_)
        SDL_WaitThread(thread_, nullptr);
}
bool YouTubeProbe::begin() noexcept
{
    if (loading())
        return false;
    result_ = {};
    pending_ = {};
    done_ = cancelled_ = false;
    stage_ = ProbeStage::dns;
    thread_ = SDL_CreateThread(worker, "tube5-direct", this);
    if (!thread_)
    {
        stage_ = ProbeStage::failed;
        std::snprintf(result_.error.data(), result_.error.size(), "NO SE PUDO CREAR HILO");
    }
    return thread_ != nullptr;
}
int YouTubeProbe::progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
{
    return static_cast<YouTubeProbe *>(opaque)->cancelled_.load() ? 1 : 0;
}
int YouTubeProbe::worker(void *opaque) noexcept
{
    auto &probe = *static_cast<YouTubeProbe *>(opaque);
    auto &out = probe.pending_;
    int dns = -1;
#ifdef TUBE5_HOST
    addrinfo hints{}, *entry = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
#ifdef TUBE5_NETWORK_WRAP
    dns = __real_getaddrinfo(host, "443", &hints, &entry);
#else
    dns = getaddrinfo(host, "443", &hints, &entry);
#endif
    out.dns_error = dns;
    if (!dns && entry)
    {
        dns = inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(entry->ai_addr)->sin_addr,
                        out.ip.data(), out.ip.size())
                  ? 0
                  : -1;
    }
    if (entry)
    {
#ifdef TUBE5_NETWORK_WRAP
        __real_freeaddrinfo(entry);
#else
        freeaddrinfo(entry);
#endif
    }
    std::snprintf(out.ca.data(), out.ca.size(), "CA DEL SISTEMA HOST - NO ES PRUEBA PS5");
#else
    dns = tube5_resolve_ipv4(host, out.ip.data(), out.ip.size(), &out.dns_error);
    std::snprintf(out.ca.data(), out.ca.size(), "%s", console_curl_ca_file());
#endif
    if (dns)
        std::snprintf(out.error.data(), out.error.size(), "DNS: NO SE PUDO RESOLVER YOUTUBE");
    else if (!probe.cancelled_)
    {
        probe.stage_ = ProbeStage::https;
        // Worker creation/join serializes accesses; curl init remains process-wide.
        (void)pthread_once(&curl_once, initialize_curl);
        CURL *easy = curl_initialized == CURLE_OK ? curl_easy_init() : nullptr;
        CURLcode code = easy ? CURLE_OK : CURLE_FAILED_INIT;
        curl_slist *resolve = nullptr;
        Body body;
        char mapping[96]{};
        std::snprintf(mapping, sizeof(mapping), "%s:443:%s", host, out.ip.data());
        if (easy)
        {
            resolve = curl_slist_append(nullptr, mapping);
            if (!resolve)
                code = CURLE_OUT_OF_MEMORY;
            const auto set = [&](CURLoption option, auto value)
            {
                if (code == CURLE_OK)
                    code = curl_easy_setopt(easy, option, value);
            };
            set(CURLOPT_URL, url);
            set(CURLOPT_RESOLVE, resolve);
            set(CURLOPT_PROXY, "");
            set(CURLOPT_NOSIGNAL, 1L);
            set(CURLOPT_PROTOCOLS_STR, "https");
            set(CURLOPT_FOLLOWLOCATION, 0L);
            set(CURLOPT_SSL_VERIFYPEER, 1L);
            set(CURLOPT_SSL_VERIFYHOST, 2L);
#ifndef TUBE5_HOST
            set(CURLOPT_CAINFO, out.ca.data());
#endif
            set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
            set(CURLOPT_USERAGENT, "Tube5-development/0.1 (direct-connectivity-probe)");
            set(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
            set(CURLOPT_TIMEOUT_MS, 10000L);
            set(CURLOPT_WRITEFUNCTION, receive);
            set(CURLOPT_WRITEDATA, &body);
            set(CURLOPT_NOPROGRESS, 0L);
            static_assert(sizeof(curl_off_t) == sizeof(long long));
            set(CURLOPT_XFERINFOFUNCTION, progress);
            set(CURLOPT_XFERINFODATA, &probe);
            if (code == CURLE_OK)
                code = curl_easy_perform(easy);
            (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &out.http);
            (void)curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &out.verify);
            curl_easy_cleanup(easy);
        }
        curl_slist_free_all(resolve);
        out.curl_error = code;
        out.bytes = static_cast<unsigned>(body.used);
        if (code != CURLE_OK)
            std::snprintf(out.error.data(), out.error.size(), "HTTPS: %s",
                          curl_easy_strerror(code));
        else if (out.http != 200)
            std::snprintf(out.error.data(), out.error.size(), "YOUTUBE: HTTP %ld", out.http);
        else if (!probe_title({body.bytes.data(), body.used}, out.title))
            std::snprintf(out.error.data(), out.error.size(),
                          "YOUTUBE: RESPUESTA NO ES METADATOS OEMBED");
    }
    probe.done_ = true;
    return 0;
}
void YouTubeProbe::poll() noexcept
{
    if (!thread_ || !done_)
        return;
    SDL_WaitThread(thread_, nullptr);
    thread_ = nullptr;
    result_ = pending_;
    stage_ = cancelled_.load()  ? ProbeStage::cancelled
             : result_.error[0] ? ProbeStage::failed
                                : ProbeStage::ready;
}
} // namespace tube5
