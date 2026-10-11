// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_probe.hpp"
#include "verified_https.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <limits>
#include <curl/curl.h>
namespace
{
constexpr const char *host = "www.youtube.com";
constexpr const char *url =
    "https://www.youtube.com/"
    "oembed?url=https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3DbHIhgxav9LY&format=json";
struct Body
{
    explicit Body(std::size_t limit = 16384)
        : bytes(new (std::nothrow) char[limit]), capacity(limit)
    {
    }
    std::unique_ptr<char[]> bytes;
    std::size_t capacity;
    std::size_t used = 0;
    bool range = false;
};
std::size_t receive(char *data, std::size_t size, std::size_t count, void *opaque)
{
    auto &body = *static_cast<Body *>(opaque);
    if (!body.bytes || (size && count > (body.capacity - body.used) / size))
        return 0;
    const auto bytes = size * count;
    std::copy_n(data, bytes, body.bytes.get() + body.used);
    body.used += bytes;
    return bytes;
}
std::size_t header(char *data, std::size_t size, std::size_t count, void *opaque)
{
    if (size && count > std::numeric_limits<std::size_t>::max() / size)
        return 0;
    const auto length = size * count;
    std::string_view line(data, length);
    constexpr std::string_view key = "content-range:";
    bool match = line.size() >= key.size();
    for (std::size_t i = 0; match && i < key.size(); ++i)
    {
        char c = line[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        match = c == key[i];
    }
    if (match)
    {
        line.remove_prefix(key.size());
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        static_cast<Body *>(opaque)->range = line.starts_with("bytes 0-4095/");
    }
    return length;
}
constexpr const char *player_url = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
// Protocol profile observed in upstream yt-dlp, not a bundled extractor.
constexpr const char *player_agent =
    "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip";
constexpr const char *player_body =
    R"({"context":{"client":{"clientName":"ANDROID","clientVersion":"21.26.364","androidSdkVersion":30,"osName":"Android","osVersion":"11","userAgent":"com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip","hl":"es","gl":"ES"}},"videoId":"bHIhgxav9LY","contentCheckOk":true,"racyCheckOk":true})";
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
bool YouTubeProbe::begin(ProbeMode mode) noexcept
{
    if (loading())
        return false;
    result_ = {};
    pending_ = {};
    result_.mode = pending_.mode = mode;
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
    const int dns = resolve_https_ipv4(host, out.ip.data(), out.ip.size(), out.dns_error);
#ifdef TUBE5_HOST
    std::snprintf(out.ca.data(), out.ca.size(), "CA DEL SISTEMA HOST - NO ES PRUEBA PS5");
#else
    std::snprintf(out.ca.data(), out.ca.size(), "%s", verified_https_ca());
#endif
    if (dns)
        std::snprintf(out.error.data(), out.error.size(), "DNS: NO SE PUDO RESOLVER YOUTUBE");
    else if (blocked_https_ipv4(out.ip.data()))
        std::snprintf(out.error.data(), out.error.size(), "DNS: DIRECCION BLOQUEADA O NO PUBLICA");
    else if (!probe.cancelled_)
    {
        probe.stage_ = ProbeStage::https;
        // Worker creation/join serializes accesses; curl init remains process-wide.
        const auto curl_initialized = initialize_verified_https();
        const auto request = [&](const char *name, const char *address, const char *target,
                                 const char *post, bool range, Body &body, long &http,
                                 long &verify) -> CURLcode
        {
            if (!body.bytes)
                return CURLE_OUT_OF_MEMORY;
            CURL *easy = curl_initialized == CURLE_OK ? curl_easy_init() : nullptr;
            CURLcode code = easy ? CURLE_OK : CURLE_FAILED_INIT;
            curl_slist *mapping_list = nullptr, *headers = nullptr;
            char mapping[300]{};
            std::snprintf(mapping, sizeof(mapping), "%s:443:%s", name, address);
            if (easy)
            {
                mapping_list = curl_slist_append(nullptr, mapping);
                if (!mapping_list)
                    code = CURLE_OUT_OF_MEMORY;
                const auto set = [&](CURLoption option, auto value)
                {
                    if (code == CURLE_OK)
                        code = curl_easy_setopt(easy, option, value);
                };
                set(CURLOPT_URL, target);
                set(CURLOPT_RESOLVE, mapping_list);
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
                set(CURLOPT_USERAGENT, out.mode == ProbeMode::formats
                                           ? player_agent
                                           : "Tube5-development/0.1 (direct-connectivity-probe)");
                set(CURLOPT_ACCEPT_ENCODING, "");
                set(CURLOPT_CONNECTTIMEOUT_MS, 5000L);
                set(CURLOPT_TIMEOUT_MS, 10000L);
                set(CURLOPT_WRITEFUNCTION, receive);
                set(CURLOPT_WRITEDATA, &body);
                set(CURLOPT_HEADERFUNCTION, header);
                set(CURLOPT_HEADERDATA, &body);
                if (post)
                {
                    headers = curl_slist_append(nullptr, "Content-Type: application/json");
                    if (!headers)
                        code = CURLE_OUT_OF_MEMORY;
                    set(CURLOPT_HTTPHEADER, headers);
                    set(CURLOPT_POSTFIELDS, post);
                    set(CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(post)));
                }
                if (range)
                    set(CURLOPT_RANGE, "0-4095");
                set(CURLOPT_NOPROGRESS, 0L);
                static_assert(sizeof(curl_off_t) == sizeof(long long));
                set(CURLOPT_XFERINFOFUNCTION, progress);
                set(CURLOPT_XFERINFODATA, &probe);
                if (code == CURLE_OK)
                    code = curl_easy_perform(easy);
                (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http);
                (void)curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &verify);
                curl_easy_cleanup(easy);
            }
            curl_slist_free_all(mapping_list);
            curl_slist_free_all(headers);
            return code;
        };
        Body body(out.mode == ProbeMode::formats ? youtube_player_limit : 16384);
        const auto code =
            request(host, out.ip.data(), out.mode == ProbeMode::formats ? player_url : url,
                    out.mode == ProbeMode::formats ? player_body : nullptr, false, body, out.http,
                    out.verify);
        out.curl_error = code;
        out.bytes = static_cast<unsigned>(body.used);
        if (code != CURLE_OK)
            std::snprintf(out.error.data(), out.error.size(), "HTTPS: %s",
                          curl_easy_strerror(code));
        else if (out.http != 200)
            std::snprintf(out.error.data(), out.error.size(), "YOUTUBE: HTTP %ld", out.http);
        else if (out.mode == ProbeMode::metadata &&
                 !probe_title({body.bytes.get(), body.used}, out.title))
            std::snprintf(out.error.data(), out.error.size(),
                          "YOUTUBE: RESPUESTA NO ES METADATOS OEMBED");
        else if (out.mode == ProbeMode::formats && !probe.cancelled_)
        {
            if (!parse_youtube_formats({body.bytes.get(), body.used}, out.formats))
                std::snprintf(out.error.data(), out.error.size(),
                              "PLAYER: JSON INVALIDO O FUERA DE LIMITES");
            else if (std::strcmp(out.formats.status.data(), "OK") != 0)
                std::snprintf(out.error.data(), out.error.size(), "PLAYER: %.40s %.40s",
                              out.formats.status.data(), out.formats.reason.data());
            else if (!out.formats.source[0])
                std::snprintf(out.error.data(), out.error.size(),
                              "FORMATOS OK - SIN MP4 AVC/AAC DIRECTO <=360P");
            else
            {
                out.title = out.formats.title;
                probe.stage_ = ProbeStage::source;
                std::array<char, 16> address{};
                int source_dns_error = 0;
                if (resolve_https_ipv4(out.formats.source_host.data(), address.data(),
                                       address.size(), source_dns_error) ||
                    blocked_https_ipv4(address.data()))
                    std::snprintf(out.error.data(), out.error.size(),
                                  "STREAM: DNS CDN FALLA O DIRECCION BLOQUEADA");
                else if (!probe.cancelled_)
                {
                    Body chunk(4096);
                    out.source_curl = request(out.formats.source_host.data(), address.data(),
                                              out.formats.source.data(), nullptr, true, chunk,
                                              out.source_http, out.source_verify);
                    out.source_bytes = static_cast<unsigned>(chunk.used);
                    out.source_mp4 = out.source_curl == CURLE_OK && out.source_http == 206 &&
                                     out.source_verify == 0 && chunk.range && chunk.used == 4096 &&
                                     std::memcmp(chunk.bytes.get() + 4, "ftyp", 4) == 0;
                    if (!out.source_mp4)
                        std::snprintf(out.error.data(), out.error.size(),
                                      "STREAM: NO VALIDADO HTTP %ld CURL %d CERT %ld",
                                      out.source_http, out.source_curl, out.source_verify);
                    else if (!probe.cancelled_)
                    {
                        probe.stage_ = ProbeStage::sponsor;
                        // Optional service: its failure must not prevent playback.
                        out.sponsor = fetch_sponsor(out.video_id.data(), probe.cancelled_);
                    }
                }
            }
        }
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
