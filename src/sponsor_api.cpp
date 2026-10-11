// SPDX-License-Identifier: GPL-3.0-or-later
#include "sponsor_api.hpp"
#include "verified_https.hpp"
#include <json-c/json.h>
#include <openssl/sha.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
namespace
{
constexpr unsigned limit = 131072;
bool identifier(std::string_view id)
{
    if (id.size() != 11)
        return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    return true;
}
json_object *get(json_object *object, const char *key)
{
    json_object *value = nullptr;
    return object && json_object_object_get_ex(object, key, &value) ? value : nullptr;
}
bool text(json_object *object, const char *key, std::string_view expected)
{
    auto *value = get(object, key);
    return value && json_object_is_type(value, json_type_string) &&
           std::string_view(json_object_get_string(value), json_object_get_string_len(value)) ==
               expected;
}
bool number(json_object *value, double &out)
{
    if (!value || !(json_object_is_type(value, json_type_int) ||
                    json_object_is_type(value, json_type_double)))
        return false;
    out = json_object_get_double(value);
    return std::isfinite(out);
}
struct Body
{
    std::unique_ptr<char[]> bytes{new (std::nothrow) char[limit]};
    unsigned used = 0;
};
std::size_t receive(char *data, std::size_t size, std::size_t count, void *opaque)
{
    auto &body = *static_cast<Body *>(opaque);
    if (!body.bytes || (size && count > (limit - body.used) / size))
        return 0;
    const auto length = size * count;
    std::memcpy(body.bytes.get() + body.used, data, length);
    body.used += length;
    return length;
}
int progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return static_cast<const std::atomic<bool> *>(opaque)->load() ? 1 : 0;
}
} // namespace
namespace tube5
{
bool parse_sponsor_response(std::string_view json, std::string_view id,
                            SponsorResponse &out) noexcept
{
    out.data = {};
    out.data.state = SponsorState::unavailable;
    out.source_duration = 0;
    if (!identifier(id) || json.empty() || json.size() > limit)
        return false;
    auto *tokener = json_tokener_new_ex(16);
    if (!tokener)
        return false;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    auto *root = json_tokener_parse_ex(tokener, json.data(), static_cast<int>(json.size()));
    bool valid = json_tokener_get_error(tokener) == json_tokener_success && root &&
                 json_object_is_type(root, json_type_array) &&
                 json_object_array_length(root) <= 256;
    const auto parsed = json_tokener_get_parse_end(tokener);
    for (std::size_t i = parsed; valid && i < json.size(); ++i)
        valid = json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t';
    std::array<SponsorRange, 128> ranges{};
    unsigned count = 0;
    bool mismatch = false, matched = false;
    for (std::size_t i = 0; valid && i < json_object_array_length(root); ++i)
    {
        auto *record = json_object_array_get_idx(root, i);
        if (!json_object_is_type(record, json_type_object))
        {
            valid = false;
            break;
        }
        if (!text(record, "videoID", id))
            continue;
        if (matched)
        {
            valid = false;
            break;
        }
        matched = true;
        auto *segments = get(record, "segments");
        if (!segments || !json_object_is_type(segments, json_type_array) ||
            json_object_array_length(segments) > ranges.size())
        {
            valid = false;
            break;
        }
        for (std::size_t j = 0; valid && j < json_object_array_length(segments); ++j)
        {
            auto *segment = json_object_array_get_idx(segments, j);
            if (!json_object_is_type(segment, json_type_object))
            {
                valid = false;
                break;
            }
            if (!text(segment, "category", "sponsor") || !text(segment, "actionType", "skip"))
                continue;
            double votes = 0, duration = 0, start = 0, end = 0;
            if (auto *v = get(segment, "votes"))
                if (!number(v, votes))
                {
                    valid = false;
                    break;
                }
            if (votes < 0)
                continue;
            if (auto *v = get(segment, "videoDuration"))
                if (!number(v, duration) || duration < 0 || duration > 86400)
                {
                    valid = false;
                    break;
                }
            if (duration)
            {
                if (out.source_duration && std::abs(out.source_duration - duration) > 2)
                    mismatch = true;
                if (!out.source_duration)
                    out.source_duration = duration;
            }
            auto *pair = get(segment, "segment");
            valid = pair && json_object_is_type(pair, json_type_array) &&
                    json_object_array_length(pair) == 2 &&
                    number(json_object_array_get_idx(pair, 0), start) &&
                    number(json_object_array_get_idx(pair, 1), end) && start >= 0 && start < end &&
                    end <= 86400;
            if (valid)
                ranges[count++] = {static_cast<unsigned>(std::llround(start * 1000)),
                                   static_cast<unsigned>(std::llround(end * 1000))};
        }
    }
    if (root)
        json_object_put(root);
    json_tokener_free(tokener);
    if (!valid)
        return false;
    if (mismatch)
    {
        out.data.state = SponsorState::unaligned;
        return true;
    }
    std::sort(ranges.begin(), ranges.begin() + count,
              [](const auto &a, const auto &b) { return a.start_ms < b.start_ms; });
    for (unsigned i = 0; i < count; ++i)
    {
        const auto &range = ranges[i];
        if (range.end_ms <= range.start_ms)
            continue;
        if (out.data.count && range.start_ms <= out.data.ranges[out.data.count - 1].end_ms)
            out.data.ranges[out.data.count - 1].end_ms =
                std::max(out.data.ranges[out.data.count - 1].end_ms, range.end_ms);
        else if (out.data.count < out.data.ranges.size())
            out.data.ranges[out.data.count++] = range;
        else
        {
            out.data = {};
            out.data.state = SponsorState::unavailable;
            return false;
        }
    }
    out.data.state = out.data.count ? SponsorState::ready : SponsorState::none;
    return true;
}
SponsorData aligned_sponsor(const SponsorResponse &response, double duration) noexcept
{
    auto data = response.data;
    if (data.state != SponsorState::ready)
        return data;
    bool mismatch = !std::isfinite(duration) || duration <= 0 || duration > 86400 ||
                    (response.source_duration && std::abs(duration - response.source_duration) > 2);
    for (unsigned i = 0; i < data.count; ++i)
        mismatch |= data.ranges[i].end_ms / 1000.0 > duration + 2;
    if (mismatch)
    {
        data = {};
        data.state = SponsorState::unaligned;
    }
    else
        for (unsigned i = 0; i < data.count; ++i)
            data.ranges[i].end_ms = std::min(data.ranges[i].end_ms,
                                             static_cast<unsigned>(std::llround(duration * 1000)));
    return data;
}
SponsorResponse fetch_sponsor(std::string_view id, const std::atomic<bool> &cancel) noexcept
{
    SponsorResponse out;
    out.data.state = SponsorState::unavailable;
    if (!identifier(id))
        return out;
    constexpr const char *host = "sponsor.ajay.app";
    std::array<char, 16> ip{};
    int dns = 0;
    if (cancel || resolve_https_ipv4(host, ip.data(), ip.size(), dns) ||
        blocked_https_ipv4(ip.data()))
    {
        std::snprintf(out.error.data(), out.error.size(), "SB: DNS FALLA O BLOQUEADO");
        return out;
    }
    unsigned char hash[SHA256_DIGEST_LENGTH]{};
    if (!SHA256(reinterpret_cast<const unsigned char *>(id.data()), id.size(), hash))
        return out;
    char url[160]{}, mapping[80]{};
    std::snprintf(url, sizeof(url),
                  "https://sponsor.ajay.app/api/skipSegments/%02x%02x?category=sponsor", hash[0],
                  hash[1]);
    std::snprintf(mapping, sizeof(mapping), "%s:443:%s", host, ip.data());
    Body body;
    CURL *easy = initialize_verified_https() == CURLE_OK && body.bytes ? curl_easy_init() : nullptr;
    auto *list = curl_slist_append(nullptr, mapping);
    CURLcode code = easy && list ? CURLE_OK : CURLE_OUT_OF_MEMORY;
    const auto set = [&](CURLoption option, auto value)
    {
        if (code == CURLE_OK)
            code = curl_easy_setopt(easy, option, value);
    };
    set(CURLOPT_URL, url);
    set(CURLOPT_RESOLVE, list);
    set(CURLOPT_PROXY, "");
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    if (const char *ca = verified_https_ca())
        set(CURLOPT_CAINFO, ca);
    set(CURLOPT_USERAGENT, "Tube5-PS5/0.1 SponsorBlock read-only");
    set(CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    set(CURLOPT_TIMEOUT_MS, 5000L);
    set(CURLOPT_WRITEFUNCTION, receive);
    set(CURLOPT_WRITEDATA, &body);
    set(CURLOPT_NOPROGRESS, 0L);
    set(CURLOPT_XFERINFOFUNCTION, progress);
    set(CURLOPT_XFERINFODATA, &cancel);
    if (code == CURLE_OK && !cancel)
        code = curl_easy_perform(easy);
    else if (cancel)
        code = CURLE_ABORTED_BY_CALLBACK;
    out.curl_error = code;
    if (easy)
    {
        (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &out.http);
        (void)curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &out.verify);
        curl_easy_cleanup(easy);
    }
    curl_slist_free_all(list);
    if (!cancel && code == CURLE_OK && out.verify == 0 && out.http == 404)
        out.data.state = SponsorState::none;
    else if (!cancel && code == CURLE_OK && out.verify == 0 && out.http == 200 &&
             parse_sponsor_response({body.bytes.get(), body.used}, id, out))
    {
    }
    else
        std::snprintf(out.error.data(), out.error.size(), "SB: HTTP %ld CURL %d CERT %ld / JSON",
                      out.http, code, out.verify);
    return out;
}
} // namespace tube5
