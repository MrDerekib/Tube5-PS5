// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_diagnostics.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <limits>
extern "C" void *__wrap_malloc(std::size_t);
extern "C" void *__wrap_calloc(std::size_t, std::size_t);
extern "C" void *__wrap_realloc(void *, std::size_t);
extern "C" int __wrap_posix_memalign(void **, std::size_t, std::size_t);
#ifdef TUBE5_ALLOC_CALLER_WRAP
extern "C" void *__wrap_av_malloc(std::size_t);
extern "C" void *__wrap_av_mallocz(std::size_t);
extern "C" void *__wrap_av_fast_realloc(void *, unsigned *, std::size_t);
#endif
namespace
{
char token;
bool fail = false;
int alignment_error = ENOMEM;
void expect(const char *expected)
{
    char text[128]{};
    tube5::memory_diagnostic_status(text, sizeof(text));
    assert(std::strcmp(text, expected) == 0);
}
} // namespace
#ifdef TUBE5_ALLOC_CALLER_WRAP
extern "C" void *__real_av_malloc(std::size_t bytes)
{
    assert(bytes == 1024 || bytes == 0);
    return fail ? nullptr : &token;
}
extern "C" void *__real_av_mallocz(std::size_t bytes)
{
    return __real_av_malloc(bytes);
}
extern "C" void *__real_av_fast_realloc(void *address, unsigned *size, std::size_t needed)
{
    assert(address == &token);
    if (needed <= *size)
        return address;
    *size = fail ? 0 : static_cast<unsigned>(needed);
    return fail ? nullptr : address;
}
#endif
extern "C" void *__real_malloc(std::size_t bytes)
{
    assert(bytes == 128 || bytes == 0);
    return fail ? nullptr : &token;
}
extern "C" void *__real_calloc(std::size_t count, std::size_t size)
{
    assert(count == 4 && size == 64);
    return fail ? nullptr : &token;
}
extern "C" void *__real_realloc(void *address, std::size_t bytes)
{
    assert(address == &token && bytes == 512);
    return fail ? nullptr : &token;
}
extern "C" int __real_posix_memalign(void **address, std::size_t alignment, std::size_t bytes)
{
    assert(alignment == 64 && bytes == 1024);
    if (fail)
        return alignment_error;
    *address = &token;
    return 0;
}
int main()
{
    tube5::reset_memory_diagnostic();
    assert(!tube5::memory_out_of_memory());
    void *aligned = nullptr;
    assert(__wrap_malloc(128) == &token && __wrap_calloc(4, 64) == &token);
    assert(__wrap_realloc(&token, 512) == &token);
    assert(__wrap_posix_memalign(&aligned, 64, 1024) == 0 && aligned == &token);
    expect("MEMFAIL 0 NONE 0B ERR 0");
    fail = true;
    assert(__wrap_malloc(128) == nullptr);
    expect("MEMFAIL 1 MALLOC 128B ERR 0");
    char first[128]{}, later[128]{};
    tube5::memory_allocation_origin_status(0, first, sizeof(first));
    assert(std::strncmp(first, "FIRSTFAIL MALLOC 128B A0 REL ", 28) == 0);
    assert(__wrap_calloc(4, 64) == nullptr);
    expect("MEMFAIL 2 CALLOC 256B ERR 0");
    assert(__wrap_realloc(&token, 512) == nullptr);
    expect("MEMFAIL 3 REALLOC 512B ERR 0");
    assert(__wrap_posix_memalign(&aligned, 64, 1024) == ENOMEM && aligned == &token);
    assert(tube5::memory_out_of_memory());
    expect("MEMFAIL 4 ALIGN 1024B ERR 12");
    tube5::memory_allocation_origin_status(0, later, sizeof(later));
    assert(std::strcmp(first, later) == 0); // First request survives later failures.
    assert(__wrap_malloc(0) == nullptr);    // A zero-size NULL is not allocation failure.
    expect("MEMFAIL 4 ALIGN 1024B ERR 12");
    tube5::reset_memory_diagnostic();
    tube5::memory_allocation_origin_status(0, later, sizeof(later));
    assert(std::strcmp(later, "FIRSTFAIL NONE") == 0);
    assert(!tube5::memory_out_of_memory());
    expect("MEMFAIL 0 NONE 0B ERR 0");
    alignment_error = EINVAL;
    assert(__wrap_posix_memalign(&aligned, 64, 1024) == EINVAL);
    assert(!tube5::memory_out_of_memory());
    expect("MEMFAIL 1 ALIGN 1024B ERR 22");
    tube5::memory_allocation_origin_status(0, first, sizeof(first));
    assert(std::strncmp(first, "FIRSTFAIL ALIGN 1024B A64 REL ", 29) == 0);
#ifdef TUBE5_ALLOC_CALLER_WRAP
    tube5::reset_memory_diagnostic();
    assert(__wrap_av_malloc(0) == nullptr);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strcmp(later, "FFALLOC NONE") == 0);
    assert(__wrap_av_malloc(1024) == nullptr);
    tube5::memory_allocation_origin_status(1, first, sizeof(first));
    assert(std::strncmp(first, "FFALLOC MALLOC 1024B A0 REL ", 27) == 0);
    assert(__wrap_av_mallocz(1024) == nullptr);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strcmp(first, later) == 0);
    tube5::reset_memory_diagnostic();
    assert(__wrap_av_mallocz(1024) == nullptr);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strncmp(later, "FFALLOC MALLOCZ 1024B A0 REL ", 28) == 0);
    fail = false;
    assert(__wrap_av_malloc(1024) == &token && __wrap_av_mallocz(1024) == &token);
    tube5::reset_memory_diagnostic();
    unsigned capacity = 200000;
    assert(__wrap_av_fast_realloc(&token, &capacity, 247680) == &token);
    assert(capacity == 247680);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strcmp(later, "FFALLOC NONE") == 0);
    fail = true;
    assert(__wrap_av_fast_realloc(&token, &capacity, 247680) == &token);
    assert(__wrap_av_fast_realloc(&token, &capacity, 300000) == nullptr);
    assert(capacity == 0);
    tube5::memory_allocation_origin_status(1, first, sizeof(first));
    assert(std::strncmp(first, "FFALLOC FASTREALLOC NEED 300000 OLD 247680 REL ", 45) == 0);
    assert(__wrap_av_mallocz(1024) == nullptr);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strcmp(first, later) == 0);
    tube5::reset_memory_diagnostic();
    capacity = 1;
    assert(__wrap_av_fast_realloc(&token, &capacity, 0) == &token);
    tube5::memory_allocation_origin_status(1, later, sizeof(later));
    assert(std::strcmp(later, "FFALLOC NONE") == 0);
#endif
}
