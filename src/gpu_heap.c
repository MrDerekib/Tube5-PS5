// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef TUBE5_GPU_PLAYER
// Share the SDK's complete allocator integration with Tube5's diagnostic and
// foreign-pointer pool wrappers. Never install a partial wrap set.
#define __wrap_malloc tube5_gpu_malloc
#define __wrap_calloc tube5_gpu_calloc
#define __wrap_realloc tube5_gpu_realloc
#define __wrap_free tube5_gpu_free
#define __wrap_posix_memalign tube5_gpu_posix_memalign
#define __wrap_malloc_usable_size tube5_gpu_malloc_usable_size
#include "../.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/native-app/app_heap.c"
#undef __wrap_malloc_usable_size
size_t __wrap_malloc_usable_size(const void *address)
{
    return tube5_gpu_malloc_usable_size(address);
}
// Optional C++ TLS hook for Mesa's statically zero-initialized context pointer.
void _ZTH23_mesa_glapi_tls_Context(void) {}
#endif
