/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TUBE5_HOST
/* Reuse the template's maintained curl/OpenSSL stand-ins and system CA path.
 * Tube5 already has a tested fcntl adapter (including commands without args).
 * The probe supplies CURLOPT_RESOLVE after explicit sceNet DNS, so curl's
 * numeric-only link wrappers remain unchanged for the validated LAN player. */
#define CONSOLE_CURL_EXTERNAL_FCNTL
#define CONSOLE_CURL_EXTERNAL_COMPAT
#include "../examples/update-check/console_curl.c"

int tube5_resolve_ipv4(const char *host, char *output, size_t capacity, int *native_error)
{
    *native_error = 0;
    const int pool = sceNetPoolCreate("tube5_dns", 16 * 1024, 0);
    if (pool < 0)
    {
        *native_error = pool;
        return -1;
    }
    const int resolver = sceNetResolverCreate("tube5_dns", pool, 0);
    int result = resolver;
    struct in_addr address = {0};
    if (resolver >= 0)
    {
        result = sceNetResolverStartNtoa(resolver, host, &address, 2000000, 1, 0);
        (void)sceNetResolverDestroy(resolver);
    }
    (void)sceNetPoolDestroy(pool);
    if (result < 0)
    {
        *native_error = result;
        return -1;
    }
    return inet_ntop(AF_INET, &address, output, (socklen_t)capacity) ? 0 : -1;
}
#endif
