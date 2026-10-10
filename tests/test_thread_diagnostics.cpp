// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "thread_diagnostics.hpp"
#include <SDL.h>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <string>
#include <vector>
extern "C" SDL_Thread *__wrap_SDL_CreateThread_REAL(SDL_ThreadFunction, const char *, void *);
extern "C" int __wrap_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *),
                                     void *);
namespace
{
int token = 0;
int thread_result = 0;
pthread_t handle{};
pthread_attr_t attributes{};
std::vector<std::string> messages;
std::vector<std::uint32_t> values;
int task(void *)
{
    return 0;
}
void *pthread_task(void *)
{
    return nullptr;
}
} // namespace
extern "C" int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                     void *(*function)(void *), void *data)
{
    assert(thread == &handle && attr == &attributes && function == pthread_task && data == &token);
    return thread_result;
}
extern "C" SDL_Thread *__real_SDL_CreateThread_REAL(SDL_ThreadFunction function, const char *,
                                                    void *data)
{
    assert(function == task && data == &token);
    if (__wrap_pthread_create(&handle, &attributes, pthread_task, data) != 0)
    {
        SDL_SetError("pthread_create failed");
        return nullptr;
    }
    return reinterpret_cast<SDL_Thread *>(&token); // Opaque token, never dereferenced.
}
extern "C" void tube5_boot_checkpoint(const char *text) noexcept
{
    messages.emplace_back(text);
}
void tube5_boot_result(const char *, std::uint32_t value) noexcept
{
    values.push_back(value);
}
int main()
{
    tube5::reset_thread_diagnostic();
    assert(__wrap_SDL_CreateThread_REAL(task, "video", &token) != nullptr);
    assert(messages.empty() && values.empty() && !tube5::thread_diagnostic()[0]);
    thread_result = EAGAIN;
    assert(__wrap_SDL_CreateThread_REAL(task, "video", &token) == nullptr);
    assert((values == std::vector<std::uint32_t>{EAGAIN}));
    assert(std::strcmp(tube5::thread_diagnostic(), "video: pthread_create failed") == 0);
    assert(messages.size() == 3 && messages[1] == "video");
    assert(__wrap_SDL_CreateThread_REAL(task, "audio", &token) == nullptr);
    assert(std::strcmp(tube5::thread_diagnostic(), "video: pthread_create failed") == 0);
    tube5::reset_thread_diagnostic();
    thread_result = 0;
    assert(__wrap_SDL_CreateThread_REAL(task, "audio", &token) != nullptr);
    assert(!tube5::thread_diagnostic()[0]);
    SDL_ClearError();
    SDL_Quit();
}
