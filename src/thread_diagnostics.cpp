// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_diagnostics.hpp"
namespace
{
char last_error[128]{};
}
namespace tube5
{
void reset_thread_diagnostic() noexcept
{
    last_error[0] = '\0';
}
const char *thread_diagnostic() noexcept
{
    return last_error;
}
} // namespace tube5
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#include "boot_diagnostics.hpp"
#include <SDL.h>
#include <cstdio>
#include <pthread.h>
extern "C" SDL_Thread *__real_SDL_CreateThread_REAL(SDL_ThreadFunction, const char *, void *);
extern "C" int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *),
                                     void *);
extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                                     void *(*function)(void *), void *data)
{
    const int result = __real_pthread_create(thread, attributes, function, data);
    if (result != 0)
        tube5_boot_result("Tube5 THREAD: pthread_create retorno",
                          static_cast<std::uint32_t>(result));
    return result;
}
extern "C" SDL_Thread *__wrap_SDL_CreateThread_REAL(SDL_ThreadFunction function, const char *name,
                                                    void *data)
{
    SDL_Thread *thread = __real_SDL_CreateThread_REAL(function, name, data);
    if (!thread)
    {
        // Keep the first failure; later threads must not overwrite its cause.
        if (!last_error[0])
            std::snprintf(last_error, sizeof(last_error), "%s: %s", name ? name : "unnamed thread",
                          SDL_GetError());
        tube5_boot_checkpoint("Tube5 THREAD: no se pudo crear");
        tube5_boot_checkpoint(name ? name : "unnamed thread");
        tube5_boot_checkpoint(SDL_GetError());
    }
    return thread;
}
#endif
