// SPDX-License-Identifier: GPL-3.0-or-later
// Offline DNS/curl doubles: exercises failure isolation without network.
#include "sponsor_api.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <curl/curl.h>
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
namespace
{
curl_write_callback writer = nullptr;
void *body = nullptr;
long http = 200, cert = 0;
CURLcode code = CURLE_OK;
bool blocked = false;
unsigned requests = 0;
std::string response = "[]";
} // namespace
extern "C" int __wrap_getaddrinfo(const char *name, const char *, const addrinfo *, addrinfo **out)
{
    assert(std::strcmp(name, "sponsor.ajay.app") == 0);
    auto *entry = static_cast<addrinfo *>(std::calloc(1, sizeof(addrinfo) + sizeof(sockaddr_in)));
    assert(entry);
    auto *address = reinterpret_cast<sockaddr_in *>(entry + 1);
    address->sin_family = AF_INET;
    assert(inet_pton(AF_INET, blocked ? "0.0.0.0" : "142.251.152.4", &address->sin_addr) == 1);
    entry->ai_addr = reinterpret_cast<sockaddr *>(address);
    *out = entry;
    return 0;
}
extern "C" void __wrap_freeaddrinfo(addrinfo *entry)
{
    std::free(entry);
}
extern "C" CURLcode __wrap_curl_easy_setopt(CURL *, int requested, ...)
{
    va_list args;
    va_start(args, requested);
    switch (static_cast<CURLoption>(requested))
    {
    case CURLOPT_WRITEFUNCTION:
        writer = va_arg(args, curl_write_callback);
        break;
    case CURLOPT_WRITEDATA:
        body = va_arg(args, void *);
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
    case CURLOPT_PROXY:
        assert(!*va_arg(args, const char *));
        break;
    case CURLOPT_PROTOCOLS_STR:
        assert(std::strcmp(va_arg(args, const char *), "https") == 0);
        break;
    case CURLOPT_URL:
    {
        const char *url = va_arg(args, const char *);
        assert(std::strstr(url, "https://sponsor.ajay.app/api/skipSegments/") == url);
        assert(!std::strstr(url, "bHIhgxav9LY")); // Only hash prefix leaves the client.
        break;
    }
    default:
        break;
    }
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode __wrap_curl_easy_getinfo(CURL *, int requested, ...)
{
    va_list args;
    va_start(args, requested);
    if (requested == CURLINFO_RESPONSE_CODE)
        *va_arg(args, long *) = http;
    else if (requested == CURLINFO_SSL_VERIFYRESULT)
        *va_arg(args, long *) = cert;
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode __wrap_curl_easy_perform(CURL *)
{
    ++requests;
    if (code != CURLE_OK)
        return code;
    return writer(response.data(), 1, response.size(), body) == response.size() ? CURLE_OK
                                                                                : CURLE_WRITE_ERROR;
}
int main()
{
    using namespace tube5;
    std::atomic<bool> cancel{false};
    const auto fetch = [&] { return fetch_sponsor("bHIhgxav9LY", cancel); };
    assert(fetch().data.state == SponsorState::none);
    http = 404;
    assert(fetch().data.state == SponsorState::none);
    http = 500;
    assert(fetch().data.state == SponsorState::unavailable);
    http = 200;
    cert = 1;
    assert(fetch().data.state == SponsorState::unavailable);
    cert = 0;
    code = CURLE_PEER_FAILED_VERIFICATION;
    assert(fetch().data.state == SponsorState::unavailable);
    code = CURLE_OPERATION_TIMEDOUT;
    assert(fetch().data.state == SponsorState::unavailable);
    code = CURLE_OK;
    response = "{}";
    assert(fetch().data.state == SponsorState::unavailable);
    response.assign(131073, ' ');
    assert(fetch().data.state == SponsorState::unavailable);
    response = "[]";
    blocked = true;
    const auto before = requests;
    assert(fetch().data.state == SponsorState::unavailable && requests == before);
    blocked = false;
    cancel = true;
    assert(fetch().data.state == SponsorState::unavailable && requests == before);
    cancel = false;
    assert(fetch().data.state == SponsorState::none);
}
