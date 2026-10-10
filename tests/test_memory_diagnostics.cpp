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
namespace
{
char token;
bool fail = false;
void expect(const char *expected)
{
    char text[128]{};
    tube5::memory_diagnostic_status(text, sizeof(text));
    assert(std::strcmp(text, expected) == 0);
}
} // namespace
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
        return ENOMEM;
    *address = &token;
    return 0;
}
int main()
{
    tube5::reset_memory_diagnostic();
    void *aligned = nullptr;
    assert(__wrap_malloc(128) == &token && __wrap_calloc(4, 64) == &token);
    assert(__wrap_realloc(&token, 512) == &token);
    assert(__wrap_posix_memalign(&aligned, 64, 1024) == 0 && aligned == &token);
    expect("MEMFAIL 0 NONE 0B ERR 0");
    fail = true;
    assert(__wrap_malloc(128) == nullptr);
    expect("MEMFAIL 1 MALLOC 128B ERR 0");
    assert(__wrap_calloc(4, 64) == nullptr);
    expect("MEMFAIL 2 CALLOC 256B ERR 0");
    assert(__wrap_realloc(&token, 512) == nullptr);
    expect("MEMFAIL 3 REALLOC 512B ERR 0");
    assert(__wrap_posix_memalign(&aligned, 64, 1024) == ENOMEM && aligned == &token);
    expect("MEMFAIL 4 ALIGN 1024B ERR 12");
    assert(__wrap_malloc(0) == nullptr); // A zero-size NULL is not allocation failure.
    expect("MEMFAIL 4 ALIGN 1024B ERR 12");
    tube5::reset_memory_diagnostic();
    expect("MEMFAIL 0 NONE 0B ERR 0");
}
