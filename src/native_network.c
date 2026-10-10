/*
 * Native IPv4 HTTP adapter, based on the resolver and SO_NBIO adapter in
 * examples/update-check/console_curl.c (BlackBearReloaded, GPL-3.0-or-later).
 * Numeric addresses only for this LAN test; no WebKit resolver imports.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(TUBE5_HOST) || defined(TUBE5_NETWORK_TEST)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                       struct addrinfo **result)
{
    if (!result)
        return EAI_FAIL;
    *result = NULL;
    const int family = hints ? hints->ai_family : AF_UNSPEC;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (hints && hints->ai_socktype && hints->ai_socktype != SOCK_STREAM &&
        hints->ai_socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    unsigned port = 0;
    if (service)
    {
        if (!*service)
            return EAI_SERVICE;
        for (const char *p = service; *p; ++p)
        {
            if (*p < '0' || *p > '9')
                return EAI_SERVICE;
            port = port * 10 + (unsigned)(*p - '0');
            if (port > 65535)
                return EAI_SERVICE;
        }
    }
    struct in_addr address;
    address.s_addr = htonl(hints && (hints->ai_flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
    if (node && inet_pton(AF_INET, node, &address) != 1)
        return EAI_NONAME;
    struct addrinfo *entry = calloc(1, sizeof(*entry) + sizeof(struct sockaddr_in) + 16);
    if (!entry)
        return EAI_MEMORY;
    struct sockaddr_in *in = (struct sockaddr_in *)(entry + 1);
#ifndef TUBE5_HOST
    in->sin_len = sizeof(*in);
#endif
    in->sin_family = AF_INET;
    in->sin_port = htons((unsigned short)port);
    in->sin_addr = address;
    entry->ai_family = AF_INET;
    entry->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    entry->ai_protocol = hints ? hints->ai_protocol : 0;
    entry->ai_addrlen = sizeof(*in);
    entry->ai_addr = (struct sockaddr *)in;
    if (node && hints && (hints->ai_flags & AI_CANONNAME))
    {
        entry->ai_canonname = (char *)(in + 1);
        (void)inet_ntop(AF_INET, &address, entry->ai_canonname, 16);
    }
    *result = entry;
    return 0;
}

void __wrap_freeaddrinfo(struct addrinfo *entry)
{
    while (entry)
    {
        struct addrinfo *next = entry->ai_next;
        free(entry);
        entry = next;
    }
}

const char *__wrap_gai_strerror(int code)
{
    switch (code)
    {
    case 0:
        return "no error";
    case EAI_NONAME:
        return "native HTTP test requires a numeric IPv4 address";
    case EAI_FAMILY:
        return "native HTTP test requires IPv4";
    case EAI_MEMORY:
        return "out of memory";
    case EAI_SERVICE:
        return "invalid numeric port";
    default:
        return "native address lookup failed";
    }
}

#ifdef TUBE5_HOST
typedef socklen_t name_buffer_size;
#else
typedef size_t name_buffer_size; /* SDK/FreeBSD getnameinfo ABI, unlike Linux. */
#endif
int __wrap_getnameinfo(const struct sockaddr *address, socklen_t length, char *host,
                       name_buffer_size host_size, char *service, name_buffer_size service_size,
                       int flags)
{
    (void)flags;
    if (!address || address->sa_family != AF_INET || length < sizeof(struct sockaddr_in))
        return EAI_FAMILY;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    if (host && (!host_size || !inet_ntop(AF_INET, &in->sin_addr, host, host_size)))
        return EAI_OVERFLOW;
    if (service)
    {
        const int size = snprintf(service, service_size, "%u", (unsigned)ntohs(in->sin_port));
        if (size < 0 || (unsigned)size >= service_size)
            return EAI_OVERFLOW;
    }
    return 0;
}

struct hostent *__wrap_gethostbyname(const char *name)
{
    (void)name;
    return NULL; /* This test uses getaddrinfo with a numeric address. */
}

extern int __real_fcntl(int descriptor, int command, ...);

int __wrap_fcntl(int descriptor, int command, ...)
{
    int value = 0;
    int result;
    va_list arguments;
    va_start(arguments, command);
    switch (command)
    {
    case F_GETFD:
    case F_GETFL:
    case F_GETOWN:
        result = __real_fcntl(descriptor, command); /* No absent vararg read. */
        break;
    case F_SETFD:
    case F_SETFL:
    case F_SETOWN:
    case F_DUPFD:
#ifdef F_DUPFD_CLOEXEC
    case F_DUPFD_CLOEXEC:
#endif
#ifdef F_DUP2FD
    case F_DUP2FD:
    case F_DUP2FD_CLOEXEC:
    case F_READAHEAD:
    case F_RDAHEAD:
#endif
        value = va_arg(arguments, int);
        result = __real_fcntl(descriptor, command, value);
        break;
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
        result = __real_fcntl(descriptor, command, va_arg(arguments, struct flock *));
        break;
    default:
        /* Reject unknown commands rather than reading a possibly absent vararg. */
        errno = EINVAL;
        result = -1;
        break;
    }
    va_end(arguments);
    if (result >= 0 || errno != EINVAL ||
        (command != F_GETFD && command != F_GETFL && command != F_SETFD && command != F_SETFL))
        return result;
    /* Do not disguise a regular file's EINVAL as a successful socket call. */
    int type = 0;
    socklen_t size = sizeof(type);
    if (getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &size) < 0)
    {
        errno = EINVAL;
        return -1;
    }
    enum
    {
        native_so_nbio = 0x1200
    };
    if (command == F_GETFD || command == F_SETFD)
        return 0; /* Native titles do not exec. */
    if (command == F_SETFL)
    {
        const int on = (value & O_NONBLOCK) != 0;
        return setsockopt(descriptor, SOL_SOCKET, native_so_nbio, &on, sizeof(on));
    }
    int on = 0;
    size = sizeof(on);
    if (getsockopt(descriptor, SOL_SOCKET, native_so_nbio, &on, &size) < 0)
        return -1;
    return O_RDWR | (on ? O_NONBLOCK : 0);
}

char *__wrap_strcasestr(const char *text, const char *needle)
{
    /* HTTP tokens are ASCII. Avoid another WebKit-only string import. */
    if (!*needle)
        return (char *)text;
    for (; *text; ++text)
    {
        const char *a = text, *b = needle;
        while (*a && *b)
        {
            const unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
            if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y))
                break;
            ++a;
            ++b;
        }
        if (!*b)
            return (char *)text;
    }
    return NULL;
}

int __wrap_isatty(int descriptor)
{
    (void)descriptor;
    errno = ENOTTY;
    return 0;
}
#endif
