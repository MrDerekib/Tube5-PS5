/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int __wrap_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void __wrap_freeaddrinfo(struct addrinfo *);
const char *__wrap_gai_strerror(int);
int __wrap_getnameinfo(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t,
                       int);
int __wrap_fcntl(int, int, ...);
char *__wrap_strcasestr(const char *, const char *);
int __wrap_isatty(int);

static int real_result = -1, real_error = EINVAL, is_socket = 1, nonblocking = 0;
static unsigned socket_checks = 0, socket_sets = 0;

int __real_fcntl(int descriptor, int command, ...)
{
    assert(descriptor == 7);
    if (command == F_SETFL || command == F_SETFD)
    {
        va_list args;
        va_start(args, command);
        (void)va_arg(args, int);
        va_end(args);
    }
    errno = real_error;
    return real_result;
}
int getsockopt(int descriptor, int level, int option, void *value, socklen_t *size)
{
    assert(descriptor == 7 && level == SOL_SOCKET && *size == sizeof(int));
    ++socket_checks;
    if (!is_socket)
    {
        errno = ENOTSOCK;
        return -1;
    }
    assert(option == SO_TYPE || option == 0x1200);
    *(int *)value = option == SO_TYPE ? SOCK_STREAM : nonblocking;
    return 0;
}
int setsockopt(int descriptor, int level, int option, const void *value, socklen_t size)
{
    assert(descriptor == 7 && level == SOL_SOCKET && option == 0x1200 && size == sizeof(int));
    nonblocking = *(const int *)value;
    ++socket_sets;
    return 0;
}
int main(void)
{
    struct addrinfo hints = {0}, *entry = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_CANONNAME;
    assert(__wrap_getaddrinfo("192.168.8.176", "8080", &hints, &entry) == 0);
    assert(entry && !entry->ai_next && entry->ai_family == AF_INET);
    const struct sockaddr_in *in = (const struct sockaddr_in *)entry->ai_addr;
    assert(ntohs(in->sin_port) == 8080 && ntohl(in->sin_addr.s_addr) == 0xc0a808b0);
    assert(strcmp(entry->ai_canonname, "192.168.8.176") == 0);
    char host[32], service[8];
    assert(__wrap_getnameinfo(entry->ai_addr, entry->ai_addrlen, host, sizeof(host), service,
                              sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0);
    assert(strcmp(host, "192.168.8.176") == 0 && strcmp(service, "8080") == 0);
    assert(__wrap_getnameinfo(entry->ai_addr, entry->ai_addrlen, host, 2, service, sizeof(service),
                              0) == EAI_OVERFLOW);
    __wrap_freeaddrinfo(entry);
    assert(__wrap_getaddrinfo("example.org", "8080", &hints, &entry) == EAI_NONAME && !entry);
    assert(__wrap_getaddrinfo("999.1.1.1", "8080", &hints, &entry) == EAI_NONAME);
    assert(__wrap_getaddrinfo("127.0.0.1", "65536", &hints, &entry) == EAI_SERVICE);
    assert(__wrap_getaddrinfo("127.0.0.1", "http", &hints, &entry) == EAI_SERVICE);
    assert(__wrap_getaddrinfo("127.0.0.1", "9999999999999999999", &hints, &entry) == EAI_SERVICE);
    hints.ai_family = AF_INET6;
    assert(__wrap_getaddrinfo("::1", "8080", &hints, &entry) == EAI_FAMILY);
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    assert(__wrap_getaddrinfo(NULL, NULL, &hints, &entry) == 0);
    assert(((struct sockaddr_in *)entry->ai_addr)->sin_addr.s_addr == htonl(INADDR_ANY));
    __wrap_freeaddrinfo(entry);
    __wrap_freeaddrinfo(NULL);
    assert(strstr(__wrap_gai_strerror(EAI_NONAME), "numeric IPv4"));

    assert(__wrap_fcntl(7, F_GETFL) == O_RDWR);
    assert(__wrap_fcntl(7, F_SETFL, O_RDWR | O_NONBLOCK) == 0 && nonblocking);
    assert(__wrap_fcntl(7, F_GETFL) == (O_RDWR | O_NONBLOCK));
    assert(__wrap_fcntl(7, F_SETFL, O_RDWR) == 0 && !nonblocking);
    assert(__wrap_fcntl(7, F_GETFD) == 0);
    assert(__wrap_fcntl(7, F_SETFD, FD_CLOEXEC) == 0);
    assert(socket_sets == 2);
    is_socket = 0;
    assert(__wrap_fcntl(7, F_GETFL) == -1 && errno == EINVAL);
    unsigned checks = socket_checks;
    real_error = EBADF;
    assert(__wrap_fcntl(7, F_GETFL) == -1 && errno == EBADF && socket_checks == checks);
    real_result = 123;
    assert(__wrap_fcntl(7, F_GETFL) == 123 && socket_checks == checks);
    assert(__wrap_fcntl(7, -999) == -1 && errno == EINVAL);
    assert(strcmp(__wrap_strcasestr("HTTP Content-Length", "content"), "Content-Length") == 0);
    assert(!__wrap_strcasestr("abc", "abcd"));
    assert(strcmp(__wrap_strcasestr("abc", ""), "abc") == 0);
    assert(__wrap_isatty(7) == 0 && errno == ENOTTY);
    puts("Native network adapters passed: IPv4, errors, socket flags and imports.");
}
