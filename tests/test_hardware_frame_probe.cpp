// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
namespace
{
struct Buffer
{
    void *data = nullptr;
    std::size_t bytes = 0;
    bool mapped = false;
};
std::array<Buffer, 5> buffers;
void *cpu = nullptr;
bool queue = false, decoder = false, delayed = false, invalid = false;
bool partial_queue = false;
int operation = 0, fail_operation = 0, delete_error = 0;
int next()
{
    return ++operation == fail_operation ? -77 : 0;
}
unsigned slot(std::int64_t address)
{
    assert(address >= 0 && address < 5);
    return address;
}
void no_resources()
{
    assert(!cpu && !queue && !decoder);
    for (const auto &buffer : buffers)
        assert(!buffer.data && !buffer.mapped);
}
void picture(tube5::VideoFrame *frame, tube5::VideoOutput *output)
{
    assert(frame->size == 32 && output->size == 56 && frame->bytes >= 3346432);
    std::memset(frame->buffer, 16, frame->bytes);
    frame->accepted = 1;
    output->valid = 1;
    output->pictures = 1;
    output->codec = 1;
    output->width = 1920;
    output->height = 1088;
    output->pitch = output->pitch_bytes = 2048;
    output->buffer = invalid ? nullptr : frame->buffer;
    output->bytes = frame->bytes;
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return 512 * 1024 * 1024;
}
extern "C" int sceKernelAvailableFlexibleMemorySize(std::size_t *bytes)
{
    *bytes = 512 * 1024 * 1024 - (cpu ? 0x4000 : 0);
    return 0;
}
extern "C" int sceKernelAllocateDirectMemory(std::int64_t start, std::int64_t end,
                                             std::size_t bytes, std::size_t alignment, int type,
                                             std::int64_t *physical)
{
    assert(!start && end == 512 * 1024 * 1024 && type == 12 && alignment == 0x4000 &&
           !(bytes % alignment));
    if (const int code = next())
        return code;
    for (unsigned i = 0; i < buffers.size(); ++i)
        if (!buffers[i].data)
        {
            assert(posix_memalign(&buffers[i].data, alignment, bytes) == 0);
            buffers[i].bytes = bytes;
            *physical = i;
            return 0;
        }
    assert(false);
    return -1;
}
extern "C" int sceKernelMapDirectMemory(void **data, std::size_t bytes, int protection, int flags,
                                        std::int64_t physical, std::size_t alignment)
{
    auto &buffer = buffers[slot(physical)];
    assert(buffer.data && !buffer.mapped && bytes == buffer.bytes && !flags &&
           alignment == 0x4000 && (protection == 0x32 || protection == 0x33));
    if (const int code = next())
        return code;
    buffer.mapped = true;
    *data = buffer.data;
    return 0;
}
extern "C" int sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t bytes)
{
    auto &buffer = buffers[slot(physical)];
    assert(buffer.data && !buffer.mapped && bytes == buffer.bytes);
    free(buffer.data);
    buffer = {};
    return 0;
}
extern "C" int sceKernelMunmap(void *data, std::size_t bytes)
{
    if (data == cpu)
    {
        free(cpu);
        cpu = nullptr;
        return 0;
    }
    for (auto &buffer : buffers)
        if (buffer.data == data)
        {
            assert(buffer.mapped && buffer.bytes == bytes);
            buffer.mapped = false;
            return 0;
        }
    assert(false);
    return -1;
}
extern "C" int sceKernelMapNamedFlexibleMemory(void **data, std::size_t bytes, int protection,
                                               int flags, const char *)
{
    assert(!cpu && protection == 3 && !flags && bytes == 0x4000);
    if (const int code = next())
        return code;
    cpu = malloc(bytes);
    assert(cpu);
    *data = cpu;
    return 0;
}
extern "C" int sceVideodec2QueryComputeMemoryInfo(tube5::VideoComputeMemory *memory)
{
    assert(memory->size == 24 && !memory->shared);
    if (const int code = next())
        return code;
    memory->bytes = 0x4000;
    return 0;
}
extern "C" int sceVideodec2AllocateComputeQueue(const tube5::VideoComputeConfig *config,
                                                const tube5::VideoComputeMemory *memory,
                                                void **handle)
{
    assert(config->size == 16 && !config->pipe && !config->queue && !config->check_memory &&
           memory->shared && !queue);
    if (const int code = next())
    {
        if (partial_queue)
        {
            queue = true;
            *handle = &queue;
        }
        return code;
    }
    queue = true;
    *handle = &queue;
    return 0;
}
extern "C" int sceVideodec2ReleaseComputeQueue(void *handle)
{
    assert(handle == &queue && queue && !decoder);
    queue = false;
    return 0;
}
extern "C" int sceVideodec2QueryDecoderMemoryInfo(const tube5::VideoDecoderConfig *config,
                                                  tube5::VideoDecoderMemory *memory)
{
    assert(queue && config->queue == reinterpret_cast<std::uintptr_t>(&queue) &&
           config->width == 1920 && config->height == 1088 && config->depth == 1 &&
           config->dpb == 4 && config->codec == 1 && config->profile == 100 && memory->size == 72);
    if (const int code = next())
        return code;
    memory->cpu_bytes = memory->gpu_bytes = memory->shared_bytes = 0x4000;
    memory->frame_bytes = 3346432;
    memory->alignment = 256;
    return 0;
}
extern "C" int sceVideodec2CreateDecoder(const tube5::VideoDecoderConfig *,
                                         const tube5::VideoDecoderMemory *memory, void **handle)
{
    assert(cpu && memory->cpu == cpu && memory->gpu && memory->shared && !decoder);
    if (const int code = next())
        return code;
    decoder = true;
    *handle = &decoder;
    return 0;
}
extern "C" int sceVideodec2DeleteDecoder(void *handle)
{
    assert(handle == &decoder && decoder && queue);
    if (delete_error)
        return delete_error;
    decoder = false;
    return 0;
}
extern "C" int sceVideodec2Reset(void *handle)
{
    assert(handle == &decoder && decoder);
    return next();
}
extern "C" int sceVideodec2Decode(void *handle, tube5::VideoInput *input, tube5::VideoFrame *frame,
                                  tube5::VideoOutput *output)
{
    assert(handle == &decoder && decoder && input->size == 48 && input->bytes > 100 &&
           input->bytes < 1024 * 1024 && input->dts == UINT64_MAX);
    if (const int code = next())
        return code;
    if (!delayed)
        picture(frame, output);
    return 0;
}
extern "C" int sceVideodec2Flush(void *, tube5::VideoFrame *frame, tube5::VideoOutput *output)
{
    if (const int code = next())
        return code;
    picture(frame, output);
    return 0;
}
int main()
{
    char directory[] = "/tmp/tube5-hwframe-XXXXXX";
    assert(mkdtemp(directory));
    char log[200]{};
    std::snprintf(log, sizeof(log), "%s/report.log", directory);
    const char *asset = "assets/hw-frame.h264";
    for (unsigned i = 0; i < 10; ++i)
    {
        operation = 0;
        assert(!tube5::hardware_frame_probe(asset, log));
        no_resources();
    }
    for (int i = 1; i <= 17; ++i)
    {
        operation = 0;
        fail_operation = i;
        assert(tube5::hardware_frame_probe(asset, log) == -77);
        no_resources();
    }
    fail_operation = 0;
    operation = 0;
    delayed = true;
    assert(!tube5::hardware_frame_probe(asset, log));
    no_resources();
    operation = 0;
    fail_operation = 18;
    assert(tube5::hardware_frame_probe(asset, log) == -77);
    no_resources();
    fail_operation = 0;
    operation = 0;
    delayed = false;
    invalid = true;
    assert(tube5::hardware_frame_probe(asset, log) != 0);
    no_resources();
    invalid = false;
    assert(tube5::hardware_frame_probe("/missing-tube5-frame", log) != 0);
    no_resources();
    const auto child = fork();
    assert(child >= 0);
    if (child == 0)
    {
        delete_error = -90;
        operation = 0;
        if (tube5::hardware_frame_probe(asset, log) != -90 || !decoder || !queue || !cpu)
            _exit(1);
        if (tube5::hardware_frame_probe(asset, log) != -EBUSY)
            _exit(2);
        _exit(0); // Quarantine deliberately persists until process exit.
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    const auto partial_child = fork();
    assert(partial_child >= 0);
    if (partial_child == 0)
    {
        operation = 0;
        fail_operation = 4;
        partial_queue = true;
        if (tube5::hardware_frame_probe(asset, log) != -77 || !queue || !buffers[0].data)
            _exit(1);
        if (tube5::hardware_frame_probe(asset, log) != -EBUSY)
            _exit(2);
        _exit(0);
    }
    assert(waitpid(partial_child, &status, 0) == partial_child && WIFEXITED(status) &&
           !WEXITSTATUS(status));
    assert(unlink(log) == 0 && rmdir(directory) == 0);
}
