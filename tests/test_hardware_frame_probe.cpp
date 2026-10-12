// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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
bool clip_motion = false;
unsigned decode_calls = 0, reset_calls = 0, last_luma = 16;
bool b_mode = false, corrupt_metadata = false, wrong_order = false, corrupt_pixels = false;
bool vp9_mode = false, retain_hidden = false;
bool vp9_4k = false;
bool vp9_pipeline = false;
bool no_accepted_frames = false;
bool vp9_hidden_fixture = false;
bool missing_hidden_output = false;
bool retain_missing_hidden = false;
bool reorder_hidden_output = false;
struct Pending
{
    tube5::VideoFrame frame{};
    std::uint64_t pts = 0, dts = 0, tag = 0;
    bool used = false;
    const unsigned char *input = nullptr;
    unsigned char input_value = 0;
};
std::array<Pending, 8> pending;
tube5::VideoPictureInfo last_picture{};
unsigned b_submitted = 0, b_next = 0;
unsigned prime_after = 4;
void reset_b_mock()
{
    pending = {};
    b_submitted = b_next = 0;
    operation = 0;
}
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
    std::memset(frame->buffer, clip_motion ? last_luma : 16, frame->bytes);
    frame->accepted = 1;
    output->valid = 1;
    output->pictures = 1;
    output->codec = vp9_mode ? 0x00245bfd : 1;
    output->width = vp9_4k ? 3840 : vp9_mode ? 2560 : 1920;
    output->height = vp9_4k ? 2160 : vp9_mode ? 1440 : 1088;
    output->pitch = output->pitch_bytes = vp9_4k ? 3840 : vp9_mode ? 2560 : 2048;
    output->buffer = invalid ? nullptr : frame->buffer;
    output->bytes = frame->bytes;
}
int consume(const tube5::VideoOutput &output, void *context) noexcept
{
    assert(output.valid == 1 && output.format == 0 && output.width == 1920 &&
           output.height == 1088 && output.pitch_bytes == 2048 && output.buffer);
    ++*static_cast<unsigned *>(context);
    return 0;
}
void b_picture(tube5::VideoOutput *output, const tube5::VideoFrame *fallback = nullptr)
{
    const unsigned wanted = wrong_order ? 3 : b_next;
    std::uint64_t oldest = UINT64_MAX;
    for (const auto &packet : pending)
        if (packet.used)
            oldest = std::min(oldest, packet.tag);
    if (reorder_hidden_output)
        for (const auto &packet : pending)
            if (packet.used && packet.input[0] == 0x84)
                oldest = packet.tag;
    for (auto &packet : pending)
        if (packet.used && (vp9_hidden_fixture ? packet.tag == oldest
                            : packet.pts == static_cast<std::uint64_t>(wanted) * 1000000 /
                                            (vp9_pipeline ? 60 : 30)))
        {
            if (packet.input)
                assert(packet.input[1] == packet.input_value); // No pending input overwritten.
            if (missing_hidden_output && packet.input[0] == 0x84)
            {
                packet.used = false;
                ++b_next;
                b_picture(output, fallback); // Driver emits only the following visible AU.
                return;
            }
            if (!packet.frame.buffer)
            {
                assert(fallback);
                packet.frame = *fallback;
            }
            picture(&packet.frame, output);
            const unsigned luma_index = vp9_hidden_fixture ? packet.input_value : wanted;
            std::memset(packet.frame.buffer, 16 + luma_index + (corrupt_pixels ? 1 : 0),
                        packet.frame.bytes);
            if (vp9_hidden_fixture && packet.input[0] == 0x84)
                output->padding = 1;
            last_picture = {};
            last_picture.size = sizeof(last_picture);
            last_picture.valid = 1;
            last_picture.pts = packet.pts;
            last_picture.dts = packet.dts;
            last_picture.attached = packet.tag;
            packet.used = false;
            ++b_next;
            return;
        }
}
void write32(int fd, std::uint32_t value)
{
    const unsigned char bytes[]{
        static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    assert(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
}
} // namespace
extern "C" std::size_t sceKernelGetDirectMemorySize()
{
    return 512 * 1024 * 1024;
}
extern "C" std::uint64_t sceKernelGetProcessTime()
{
    static std::uint64_t clock = 1000;
    clock += 1000;
    return clock;
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
           config->width == (vp9_4k     ? 3840
                             : vp9_mode ? 2560
                                        : 1920) &&
           config->height == (vp9_4k     ? 2160
                              : vp9_mode ? 1440
                                         : 1088) &&
           config->depth == (b_mode ? 3u : 1u) && config->dpb == 4 &&
           config->codec == (vp9_mode ? 0x00245bfdu : 1u) &&
           config->profile == (vp9_mode ? 0u : 100u) && memory->size == 72);
    if (const int code = next())
        return code;
    memory->cpu_bytes = memory->gpu_bytes = memory->shared_bytes = 0x4000;
    memory->frame_bytes = vp9_4k ? 12533760 : vp9_mode ? 5668864 : 3346432;
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
    ++reset_calls;
    return next();
}
extern "C" int sceVideodec2Decode(void *handle, tube5::VideoInput *input, tube5::VideoFrame *frame,
                                  tube5::VideoOutput *output)
{
    if (vp9_mode && !vp9_pipeline)
    {
        assert(handle == &decoder && decoder && (input->attached || vp9_4k) && input->bytes == 101);
        if (*static_cast<unsigned char *>(input->au) == 0x84)
            frame->accepted = retain_hidden ? 1 : 0;
        else
        {
            picture(frame, output);
            if (*static_cast<unsigned char *>(input->au) == 0x85)
                output->padding = 1; // Observed on a valid VP9 hidden surface.
        }
        return 0;
    }
    if (b_mode)
    {
        assert(handle == &decoder && decoder &&
               (vp9_pipeline ? input->attached != 0 : input->attached == b_submitted + 1) &&
               input->bytes == 101);
        if (const int code = next())
            return code;
        auto &packet = pending[b_submitted];
        assert(!packet.used);
        for (const auto &retained : pending)
            if (retained.used && retained.frame.buffer)
                assert(retained.frame.buffer != frame->buffer);
        packet = {*frame, input->pts, input->dts, input->attached, true};
        packet.input = static_cast<const unsigned char *>(input->au);
        packet.input_value = packet.input[1];
        frame->accepted = no_accepted_frames ||
                                  (missing_hidden_output && packet.input[0] == 0x84 &&
                                   !retain_missing_hidden) ? 0 : 1;
        if (!frame->accepted)
            packet.frame.buffer = nullptr;
        if (++b_submitted >= prime_after)
            b_picture(output, frame);
        return 0;
    }
    assert(handle == &decoder && decoder && input->size == 48 && input->bytes > 100 &&
           input->bytes < 1024 * 1024 && input->dts == UINT64_MAX);
    if (clip_motion)
    {
        assert(input->pts == static_cast<std::uint64_t>(decode_calls) * 1000000 / 30);
        last_luma = 16 + (++decode_calls % 120);
    }
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
    if (b_mode)
        b_picture(output, frame);
    else
        picture(frame, output);
    return 0;
}
extern "C" int sceVideodec2GetPictureInfo(const tube5::VideoOutput *output,
                                          tube5::VideoPictureInfo *info, void *second)
{
    assert(b_mode && output->valid && info->size == 120 && !second);
    *info = last_picture;
    if (corrupt_metadata)
        info->attached = 0;
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--vp9-seek-priming") == 0)
    {
        vp9_mode = vp9_4k = vp9_pipeline = b_mode = vp9_hidden_fixture = true;
        no_accepted_frames = missing_hidden_output = true;
        prime_after = 6;
        assert(!tube5::hardware_stream_begin(tube5::HardwareFrameMode::vp9_2160, true));
        assert(tube5::hardware_stream_status().input_slots == 6);
        assert(tube5::hardware_stream_status().slots == 4);
        bool input_allocation = false;
        for (const auto &buffer : buffers)
            input_allocation |= buffer.bytes == 6 * 1024 * 1024;
        assert(input_allocation);
        tube5::VideoOutput output{};
        tube5::VideoPictureInfo info{};
        unsigned char bytes[101]{};
        for (unsigned cycle = 0; cycle < 2; ++cycle)
        {
            assert(!tube5::hardware_stream_reset());
            reset_b_mock();
            unsigned delivered = 0;
            const auto collect = [&]()
            {
                const int rc = tube5::hardware_stream_receive(output, info);
                if (!rc)
                {
                    assert(delivered < 7);
                    assert(info.pts == static_cast<std::uint64_t>(delivered) * 1000000 / 60);
                    assert(*static_cast<unsigned char *>(output.buffer) == 16 + delivered);
                    ++delivered;
                    tube5::hardware_stream_release();
                }
                else
                    assert(rc == 1 || rc == 2);
                return rc;
            };
            for (unsigned au = 0; au < 8; ++au)
            {
                const unsigned index = au ? au == 1 ? 1 : au - 1 : 0;
                bytes[0] = au == 1 ? 0x84 : 0x86;
                bytes[1] = index;
                assert(!tube5::hardware_stream_send(bytes, sizeof(bytes),
                           static_cast<std::uint64_t>(index) * 1000000 / 60, index, au != 1));
                if (au < 5)
                    assert(tube5::hardware_stream_receive(output, info) == 2);
                else
                    collect();
            }
            assert(tube5::hardware_stream_status().pending_peak == 6);
            assert(!tube5::hardware_stream_send(nullptr, 0, 0, 0));
            unsigned drains = 0;
            while (collect() != 1)
                assert(++drains <= 6);
            assert(delivered == 7 && !tube5::hardware_stream_status().error);
        }
        assert(!tube5::hardware_stream_close());
        no_resources();
        std::puts("VP9 seek priming six retained AUs/four surfaces, hidden omission, reset, PTS and drain passed");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--vp9-async-hidden-stream") == 0)
    {
        vp9_mode = vp9_4k = vp9_pipeline = b_mode = vp9_hidden_fixture = true;
        assert(!tube5::hardware_stream_begin(tube5::HardwareFrameMode::vp9_2160, true));
        tube5::VideoOutput output{};
        tube5::VideoPictureInfo info{};
        unsigned char bytes[101]{};
        for (unsigned scenario : {0u, 1u, 2u})
        {
            assert(!tube5::hardware_stream_reset());
            reset_b_mock();
            missing_hidden_output = scenario == 1;
            reorder_hidden_output = scenario == 2;
            unsigned delivered = 0;
            const auto collect = [&]()
            {
                const int rc = tube5::hardware_stream_receive(output, info);
                if (!rc)
                {
                    assert(delivered < 3 && output.width == 3840 && output.height == 2160);
                    assert(info.pts == static_cast<std::uint64_t>(delivered) * 1000000 / 60);
                    assert(*static_cast<unsigned char *>(output.buffer) == 16 + delivered);
                    ++delivered;
                    tube5::hardware_stream_release();
                }
                else
                    assert(rc == 1 || rc == 2);
                return rc;
            };
            // Same sequence as a split superframe: key, hidden, visible, visible.
            for (unsigned au = 0; au < 4; ++au)
            {
                const unsigned index = au ? au == 3 ? 2 : 1 : 0;
                bytes[0] = au == 1 ? 0x84 : 0x86;
                bytes[1] = index;
                assert(!tube5::hardware_stream_send(bytes, sizeof(bytes),
                           static_cast<std::uint64_t>(index) * 1000000 / 60, index, au != 1));
                collect();
            }
            assert(!tube5::hardware_stream_send(nullptr, 0, 0, 0));
            unsigned drains = 0;
            while (collect() != 1)
                assert(++drains <= 4);
            assert(delivered == 3 && tube5::hardware_stream_status().hidden == scenario + 1);
            assert(!tube5::hardware_stream_status().policy_error);
            assert(tube5::hardware_stream_status().hidden_omitted == (scenario >= 1 ? 1u : 0u));
            // A seek may abandon hidden and visible AUs together.
            assert(!tube5::hardware_stream_reset());
            reset_b_mock();
            for (unsigned au = 0; au < 3; ++au)
            {
                bytes[0] = au == 1 ? 0x84 : 0x86;
                bytes[1] = au;
                assert(!tube5::hardware_stream_send(bytes, sizeof(bytes), au, au, au != 1));
                assert(tube5::hardware_stream_receive(output, info) == 2);
            }
        }
        assert(!tube5::hardware_stream_close());
        no_resources();
        std::puts("VP9 async hidden stream flags, omitted/materialized/overtaking, PTS, drain and seek reset passed");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--vp9-async-stream") == 0)
    {
        vp9_mode = vp9_4k = vp9_pipeline = b_mode = true;
        assert(!tube5::hardware_stream_begin(tube5::HardwareFrameMode::vp9_2160, true));
        unsigned char payload[101]{};
        payload[0] = 0x86;
        tube5::VideoOutput output{};
        tube5::VideoPictureInfo info{};
        const auto verify = [&](unsigned i)
        {
            assert(output.width == 3840 && output.height == 2160);
            assert(info.pts == static_cast<std::uint64_t>(i) * 1000000 / 60);
            assert(*static_cast<const unsigned char *>(output.buffer) == 16 + i);
            assert(tube5::hardware_stream_receive(output, info) == 2);
            assert(tube5::hardware_stream_send(payload, sizeof(payload), 0, 0) == 2);
            tube5::hardware_stream_release();
        };
        for (bool unaccepted : {false, true})
        {
            no_accepted_frames = unaccepted;
            unsigned received = 0;
            for (unsigned i = 0; i < 8; ++i)
            {
                payload[1] = i;
                assert(!tube5::hardware_stream_send(payload, sizeof(payload),
                           static_cast<std::uint64_t>(i) * 1000000 / 60, i));
                const int rc = tube5::hardware_stream_receive(output, info);
                if (i < 3)
                    assert(rc == 2);
                else
                {
                    assert(!rc && info.dts == received);
                    verify(received++);
                }
            }
            assert(!tube5::hardware_stream_send(nullptr, 0, 0, 0));
            while (received < 8)
            {
                assert(!tube5::hardware_stream_receive(output, info) && info.dts == received);
                verify(received++);
            }
            assert(tube5::hardware_stream_receive(output, info) == 1);
            assert(tube5::hardware_stream_status().pending_peak == 4);
            assert(tube5::hardware_stream_status().depth == 3);
            assert(tube5::hardware_stream_status().slots == 4);
            assert(!tube5::hardware_stream_reset());
            reset_b_mock();
            // Seek/reset while inputs and retained surfaces are still in flight.
            for (unsigned i = 0; i < 3; ++i)
            {
                payload[1] = i;
                assert(!tube5::hardware_stream_send(payload, sizeof(payload),
                           static_cast<std::uint64_t>(i) * 1000000 / 60, i));
                assert(tube5::hardware_stream_receive(output, info) == 2);
            }
            assert(!tube5::hardware_stream_reset());
            reset_b_mock();
        }
        assert(!tube5::hardware_stream_close());
        no_resources();
        std::puts("VP9 async stream FIFO, retained buffers, backpressure, drain and seek reset passed");
        return 0;
    }
    if (argc == 2 &&
        (std::strcmp(argv[1], "--vp9-4k") == 0 || std::strcmp(argv[1], "--vp9-4k60") == 0 ||
         std::strcmp(argv[1], "--vp9-pipeline") == 0 ||
         std::strcmp(argv[1], "--vp9-hidden-pipeline") == 0))
    {
        vp9_mode = vp9_4k = true;
        vp9_hidden_fixture = std::strcmp(argv[1], "--vp9-hidden-pipeline") == 0;
        vp9_pipeline = b_mode = vp9_hidden_fixture || std::strcmp(argv[1], "--vp9-pipeline") == 0;
        const bool sixty = vp9_pipeline || std::strcmp(argv[1], "--vp9-4k60") == 0;
        const unsigned count = vp9_hidden_fixture ? 3 : vp9_pipeline ? 8 : 2;
        char folder[] = "/tmp/tube5-vp9-4k-XXXXXX";
        assert(mkdtemp(folder));
        char asset[200]{}, log[200]{};
        std::snprintf(asset, sizeof(asset), "%s/clip.t5hc", folder);
        std::snprintf(log, sizeof(log), "%s/decoder.log", folder);
        const int fd = open(asset, O_CREAT | O_WRONLY | O_EXCL, 0600);
        assert(fd >= 0 && write(fd, "T5HC", 4) == 4);
        for (auto value : {vp9_hidden_fixture ? 7u : sixty ? 6u : 5u, 3840u, 2160u, sixty ? 60u : 30u, count, 2u})
            write32(fd, value);
        unsigned char payload[101]{};
        payload[0] = 0x86;
        for (unsigned index = 0; index < count; ++index)
        {
            std::uint32_t crc = UINT32_MAX;
            for (unsigned y = 0; y < 2160; y += 31)
                for (unsigned x = 0; x < 3840; x += 31)
                {
                    crc ^= 16u + (vp9_pipeline ? index : 0);
                    for (unsigned bit = 0; bit < 8; ++bit)
                        crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0);
                }
            payload[1] = index;
            const bool hidden_packet = vp9_hidden_fixture && index == 1;
            for (auto value : {hidden_packet ? 206u : 101u, index, index, crc ^ UINT32_MAX})
                write32(fd, value);
            if (hidden_packet)
            {
                unsigned char hidden[101]{};
                hidden[0] = 0x84;
                hidden[1] = index;
                assert(write(fd, hidden, sizeof(hidden)) == sizeof(hidden));
            }
            assert(write(fd, payload, sizeof(payload)) == sizeof(payload));
            if (hidden_packet)
            {
                const unsigned char suffix[]{0xc1, 101, 101, 0xc1};
                assert(write(fd, suffix, sizeof(suffix)) == sizeof(suffix));
            }
        }
        close(fd);
        assert(!tube5::hardware_clip_begin(asset, log));
        assert(tube5::hardware_clip_status().fps == (sixty ? 60u : 30u));
        unsigned shown = 0;
        const auto consume4k = [](const tube5::VideoOutput &output, void *context) noexcept
        {
            assert(output.width == 3840 && output.height == 2160 && output.pitch_bytes == 3840);
            ++*static_cast<unsigned *>(context);
            return 0;
        };
        for (unsigned steps = 0; shown < count; ++steps)
        {
            assert(steps < count + 12);
            const int rc = tube5::hardware_clip_step(consume4k, &shown);
            assert(rc == 0 || (vp9_pipeline && rc == 2));
            if (!rc)
                tube5::hardware_clip_presented(1);
        }
        assert(tube5::hardware_clip_step(consume4k, &shown) == 1);
        assert(shown == count && tube5::hardware_clip_status().crc_matches == count);
        if (vp9_pipeline)
        {
            assert(tube5::hardware_clip_status().no_output == 3);
            assert(tube5::hardware_clip_status().drain_calls == 3);
            if (vp9_hidden_fixture)
            {
                assert(tube5::hardware_clip_status().hidden_frames == 1);
                assert(tube5::hardware_clip_status().superframes == 1);
                assert(tube5::hardware_clip_status().coded_frames == count + 1);
            }
        }
#ifdef TUBE5_HW_TIMING
        const auto &timing = tube5::hardware_clip_status();
        assert((vp9_pipeline || timing.last_read_us > 0) && timing.last_decode_us > 0 && timing.last_crc_us > 0 &&
               timing.last_consumer_us > 0 &&
               timing.last_step_us >= timing.last_read_us + timing.last_decode_us +
                                          timing.last_crc_us + timing.last_consumer_us);
#endif
        assert(!tube5::hardware_clip_close());
        no_resources();
        if (vp9_hidden_fixture)
            for (unsigned scenario : {0u, 1u, 2u, 3u})
            {
                reset_b_mock();
                corrupt_pixels = scenario == 0;
                missing_hidden_output = scenario == 1 || scenario == 2;
                retain_missing_hidden = scenario == 2;
                reorder_hidden_output = scenario == 3;
                assert(!tube5::hardware_clip_begin(asset, log));
                unsigned delivered = 0;
                int rc = 0;
                for (unsigned i = 0; i < count * 2 + 12 && rc >= 0; ++i)
                {
                    rc = tube5::hardware_clip_step(consume4k, &delivered);
                    if (rc == 1)
                        break;
                }
                if (scenario == 1 || scenario == 3)
                {
                    if (rc != 1 || delivered != count)
                        std::fprintf(stderr, "hidden omitted rc=%d delivered=%u\n", rc, delivered);
                    assert(rc == 1 && delivered == count);
                    assert(tube5::hardware_clip_status().hidden_no_output == (scenario == 1 ? 1u : 0u));
                    assert(tube5::hardware_clip_status().crc_matches == count);
                }
                else
                    assert(scenario == 2 ? rc == -ENOBUFS || rc == -EINVAL : rc == -EILSEQ);
                assert(!tube5::hardware_clip_close());
                no_resources();
            }
        if (vp9_pipeline && !vp9_hidden_fixture)
        {
            reset_b_mock();
            no_accepted_frames = true;
            assert(!tube5::hardware_clip_begin(asset, log));
            unsigned delivered = 0;
            for (unsigned steps = 0; delivered < count; ++steps)
            {
                assert(steps < count + 12);
                const int rc = tube5::hardware_clip_step(consume4k, &delivered);
                assert(rc == 0 || rc == 2);
            }
            assert(tube5::hardware_clip_step(consume4k, &delivered) == 1);
            assert(tube5::hardware_clip_status().crc_matches == count);
            assert(!tube5::hardware_clip_close());
            no_resources();
            no_accepted_frames = false;
        }
        if (vp9_pipeline && !vp9_hidden_fixture)
            for (bool bad_order : {false, true})
            {
                reset_b_mock();
                corrupt_pixels = !bad_order;
                wrong_order = bad_order;
                assert(!tube5::hardware_clip_begin(asset, log));
                int rc = 0;
                for (unsigned i = 0; i < 4 && rc >= 0; ++i)
                    rc = tube5::hardware_clip_step(consume4k, &shown);
                assert(rc == -EILSEQ); // FIFO misassociation or altered pixels cannot pass CRC.
                assert(!tube5::hardware_clip_close());
                no_resources();
            }
        if (sixty)
        {
            const int wrong = open(asset, O_WRONLY);
            const unsigned char fps30[]{30, 0, 0, 0};
            assert(wrong >= 0 && pwrite(wrong, fps30, 4, 16) == 4);
            close(wrong);
            assert(tube5::hardware_clip_begin(asset, log) == -EINVAL);
            no_resources();
        }
        unlink(asset);
        unlink(log);
        rmdir(folder);
        std::puts("VP9 4K clip schema, frame bounds, CRC and cleanup passed");
        return 0;
    }
    if (argc == 2 &&
        (std::strcmp(argv[1], "--vp9") == 0 || std::strcmp(argv[1], "--vp9-stream-4k") == 0))
    {
        vp9_mode = true;
        vp9_4k = std::strcmp(argv[1], "--vp9-stream-4k") == 0;
        const auto mode =
            vp9_4k ? tube5::HardwareFrameMode::vp9_2160 : tube5::HardwareFrameMode::vp9_1440;
        assert(!tube5::hardware_stream_begin(mode));
        unsigned char bytes[101]{};
        tube5::VideoOutput output{};
        tube5::VideoPictureInfo info{};
        bytes[0] = 0x84;
        assert(!tube5::hardware_stream_send(bytes, sizeof(bytes), 100, 80, false));
        assert(tube5::hardware_stream_receive(output, info) == 2);
        bytes[0] = 0x85;
        assert(!tube5::hardware_stream_send(bytes, sizeof(bytes), 100, 80, false));
        assert(tube5::hardware_stream_receive(output, info) == 2);
        bytes[0] = 0x86;
        assert(!tube5::hardware_stream_send(bytes, sizeof(bytes), 100, 80));
        assert(!tube5::hardware_stream_receive(output, info));
        assert(output.width == (vp9_4k ? 3840u : 2560u) &&
               output.height == (vp9_4k ? 2160u : 1440u) && info.pts == 100 && info.dts == 80);
        assert(tube5::hardware_stream_send(bytes, sizeof(bytes), 200, 180) == 2);
        tube5::hardware_stream_release();
        assert(tube5::hardware_stream_status().hidden == 2);
        assert(!tube5::hardware_stream_reset());
        assert(!tube5::hardware_stream_send(bytes, sizeof(bytes), 200, 180));
        assert(!tube5::hardware_stream_receive(output, info) && info.pts == 200);
        tube5::hardware_stream_release();
        assert(!tube5::hardware_stream_send(nullptr, 0, 0, 0));
        assert(tube5::hardware_stream_receive(output, info) == 1);
        assert(!tube5::hardware_stream_close());
        no_resources();
        assert(!tube5::hardware_stream_begin(mode));
        bytes[0] = 0x84;
        retain_hidden = true;
        assert(tube5::hardware_stream_send(bytes, sizeof(bytes), 0, 0, false) == -EINVAL);
        assert(!tube5::hardware_stream_close());
        no_resources();
        retain_hidden = false;
        invalid = true;
        assert(!tube5::hardware_stream_begin(mode));
        bytes[0] = 0x86;
        assert(tube5::hardware_stream_send(bytes, sizeof(bytes), 0, 0) == -EINVAL);
        assert(!tube5::hardware_stream_close());
        no_resources();
        std::puts("VP9 stream ownership, hidden outputs, timestamps, reset and cleanup passed");
        return 0;
    }
    char directory[] = "/tmp/tube5-hwframe-XXXXXX";
    assert(mkdtemp(directory));
    char log[200]{};
    std::snprintf(log, sizeof(log), "%s/report.log", directory);
    const char *asset = "assets/hw-frame.h264";
    operation = 0;
    assert(!tube5::hardware_frame_cycles(asset, directory));
    assert(operation == 8 * 17);
    no_resources();
    operation = 0;
    fail_operation = 3;
    assert(tube5::hardware_frame_cycles(asset, directory) == -77 && operation == 3);
    no_resources();
    fail_operation = 0;
    assert(tube5::hardware_frame_cycles(asset, "/missing-tube5-directory") != 0);
    assert(operation == 3); // No decode when the progress log cannot be written.
    for (unsigned i = 0; i < 10; ++i)
    {
        operation = 0;
        assert(!tube5::hardware_frame_probe(asset, log));
        no_resources();
    }
    unsigned consumed = 0;
    operation = 0;
    assert(!tube5::hardware_frame_probe(asset, log, consume, &consumed));
    assert(consumed == 1);
    no_resources();
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
    // Keep one decoder across 150 access units, including depth-one deferred output.
    clip_motion = true;
    delayed = true;
    operation = 0;
    decode_calls = 0;
    const auto resets_before = reset_calls;
    assert(!tube5::hardware_clip_begin("assets/hw-clip.t5hc", log));
    assert(tube5::hardware_frame_probe(asset, log) == -EBUSY);
    consumed = 0;
    for (unsigned i = 0; i < 150; ++i)
    {
        assert(!tube5::hardware_clip_step(consume, &consumed));
        tube5::hardware_clip_presented(50);
    }
    assert(tube5::hardware_clip_step(consume, &consumed) == 1 && consumed == 150);
    assert(reset_calls == resets_before + 1 && decode_calls == 150);
    assert(!tube5::hardware_clip_close());
    const auto &clip_result = tube5::hardware_clip_status();
    assert(!clip_result.active && !clip_result.error && clip_result.submitted == 150 &&
           clip_result.decoded == 150 && clip_result.presented == 150 &&
           clip_result.changed_frames == 149 && clip_result.copy_max_us == 50);
    no_resources();
    operation = 0;
    decode_calls = 0;
    assert(!tube5::hardware_clip_begin("assets/hw-clip.t5hc", log));
    assert(!tube5::hardware_clip_step(consume, &consumed));
    assert(!tube5::hardware_clip_close(true) && tube5::hardware_clip_status().cancelled);
    no_resources();
    operation = 0;
    decode_calls = 0;
    assert(!tube5::hardware_clip_begin("assets/hw-clip.t5hc", log));
    invalid = true;
    assert(tube5::hardware_clip_step(consume, &consumed) == -EINVAL);
    assert(!tube5::hardware_clip_close());
    invalid = false;
    no_resources();
    operation = 0;
    decode_calls = 0;
    assert(!tube5::hardware_clip_begin("assets/hw-clip.t5hc", log));
    const auto rejected = +[](const tube5::VideoOutput &, void *) noexcept { return -88; };
    assert(tube5::hardware_clip_step(rejected, nullptr) == -88);
    const auto calls_after_error = decode_calls;
    assert(tube5::hardware_clip_step(rejected, nullptr) == -88 &&
           decode_calls == calls_after_error);
    assert(!tube5::hardware_clip_close());
    no_resources();
    assert(tube5::hardware_clip_begin(asset, log) == -EINVAL);
    no_resources();
    clip_motion = false;
    delayed = false;
    char basset[200]{};
    std::snprintf(basset, sizeof(basset), "%s/bclip.t5hc", directory);
    const int fd = open(basset, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && write(fd, "T5HC", 4) == 4);
    for (auto value : {2u, 1920u, 1080u, 30u, 8u, 1u})
        write32(fd, value);
    constexpr unsigned order[]{0, 3, 1, 2, 6, 4, 5, 7};
    // Checksums independently generated with Python zlib for 2170 constant samples.
    constexpr std::uint32_t crc[]{0x4ecf7cb8, 0x02fb0f21, 0xd6a79b8a, 0x9a93e813,
                                  0xa56fb49d, 0xe95bc704, 0x3d0753af, 0x71332036};
    unsigned char payload[101]{};
    for (unsigned i = 0; i < 8; ++i)
    {
        for (auto value : {101u, order[i], i, crc[order[i]]})
            write32(fd, value);
        assert(write(fd, payload, sizeof(payload)) == sizeof(payload));
    }
    assert(close(fd) == 0);
    b_mode = true;
    reset_b_mock();
    consumed = 0;
    assert(!tube5::hardware_clip_begin(basset, log));
    for (unsigned step = 0; step < 20; ++step)
    {
        const int code = tube5::hardware_clip_step(consume, &consumed);
        if (code == 1)
            break;
        assert(code == 0 || code == 2);
        if (code == 0)
            tube5::hardware_clip_presented(25);
    }
    assert(consumed == 8 && tube5::hardware_clip_status().metadata == 8 &&
           tube5::hardware_clip_status().crc_matches == 8 &&
           tube5::hardware_clip_status().no_output == 3 &&
           tube5::hardware_clip_status().drain_calls == 3);
    assert(!tube5::hardware_clip_close());
    no_resources();
    reset_b_mock();
    assert(!tube5::hardware_clip_begin(basset, log));
    assert(tube5::hardware_clip_step(consume, &consumed) == 2);
    assert(tube5::hardware_clip_step(consume, &consumed) == 2);
    assert(!tube5::hardware_clip_close(true) && tube5::hardware_clip_status().cancelled);
    no_resources();
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        reset_b_mock();
        corrupt_metadata = scenario == 0;
        wrong_order = scenario == 1;
        corrupt_pixels = scenario == 2;
        assert(!tube5::hardware_clip_begin(basset, log));
        for (unsigned i = 0; i < 3; ++i)
            assert(tube5::hardware_clip_step(consume, &consumed) == 2);
        assert(tube5::hardware_clip_step(consume, &consumed) ==
               (scenario == 2 ? -EILSEQ : -EINVAL));
        assert(!tube5::hardware_clip_close());
        no_resources();
    }
    corrupt_metadata = wrong_order = corrupt_pixels = false;
    reset_b_mock();
    assert(!tube5::hardware_stream_begin());
    assert(tube5::hardware_clip_begin(basset, log) == -EBUSY);
    unsigned stream_frames = 0;
    tube5::VideoOutput stream_output{};
    tube5::VideoPictureInfo stream_info{};
    for (unsigned i = 0; i < 8; ++i)
    {
        assert(!tube5::hardware_stream_send(payload, sizeof(payload),
                                            static_cast<std::uint64_t>(order[i]) * 1000000 / 30,
                                            static_cast<std::uint64_t>(i) * 1000000 / 30));
        const int code = tube5::hardware_stream_receive(stream_output, stream_info);
        assert(code == (i < 3 ? 2 : 0));
        if (!code)
        {
            assert(stream_info.pts == static_cast<std::uint64_t>(stream_frames++) * 1000000 / 30);
            assert(tube5::hardware_stream_send(payload, sizeof(payload), 0, 0) == 2);
            tube5::hardware_stream_release();
        }
    }
    assert(!tube5::hardware_stream_send(nullptr, 0, 0, 0));
    while (tube5::hardware_stream_receive(stream_output, stream_info) == 0)
    {
        assert(stream_info.pts == static_cast<std::uint64_t>(stream_frames++) * 1000000 / 30);
        tube5::hardware_stream_release();
    }
    assert(stream_frames == 8 && tube5::hardware_stream_status().received == 8);
    const auto prior_resets = tube5::hardware_stream_status().resets;
    assert(!tube5::hardware_stream_reset());
    assert(!tube5::hardware_stream_status().error &&
           tube5::hardware_stream_status().resets == prior_resets + 1);
    tube5::hardware_stream_close();
    no_resources();
    b_mode = false;
    assert(unlink(basset) == 0);
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
    char batch_path[200]{};
    for (unsigned i = 1; i <= 8; ++i)
    {
        std::snprintf(batch_path, sizeof(batch_path), "%s/tube5-hardware-cycle-%u.log", directory,
                      i);
        assert(unlink(batch_path) == 0);
    }
    std::snprintf(batch_path, sizeof(batch_path), "%s/tube5-hardware-cycles.log", directory);
    assert(unlink(batch_path) == 0);
    assert(unlink(log) == 0 && rmdir(directory) == 0);
}
