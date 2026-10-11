// SPDX-License-Identifier: GPL-3.0-or-later
#include "verified_https.hpp"
#include <cstdio>
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
extern "C" int tube5_resolve_ipv4(const char *, char *, std::size_t, int *);
extern "C" const char *console_curl_ca_file(void);
#endif
namespace
{
pthread_once_t once = PTHREAD_ONCE_INIT;
CURLcode initialized = CURLE_FAILED_INIT;
void initialize()
{
    initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
}
} // namespace
namespace tube5
{
CURLcode initialize_verified_https() noexcept
{
    (void)pthread_once(&once, initialize);
    return initialized;
}
int resolve_https_ipv4(const char *name, char *ip, std::size_t capacity, int &error) noexcept
{
#ifdef TUBE5_HOST
    addrinfo hints{}, *entry = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
#ifdef TUBE5_NETWORK_WRAP
    const int result = __real_getaddrinfo(name, "443", &hints, &entry);
#else
    const int result = getaddrinfo(name, "443", &hints, &entry);
#endif
    error = result;
    const bool ok = !result && entry &&
                    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(entry->ai_addr)->sin_addr,
                              ip, capacity);
    if (entry)
    {
#ifdef TUBE5_NETWORK_WRAP
        __real_freeaddrinfo(entry);
#else
        freeaddrinfo(entry);
#endif
    }
    return ok ? 0 : -1;
#else
    return tube5_resolve_ipv4(name, ip, capacity, &error);
#endif
}
bool blocked_https_ipv4(const char *text) noexcept
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 ||
        d > 255)
        return true;
    return a == 0 || a == 10 || a == 127 || a >= 224 || (a == 169 && b == 254) ||
           (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 100 && b >= 64 && b <= 127);
}
const char *verified_https_ca() noexcept
{
#ifdef TUBE5_HOST
    return nullptr; // libcurl's system CA configuration, not a console CA.
#else
    return console_curl_ca_file();
#endif
}
} // namespace tube5
