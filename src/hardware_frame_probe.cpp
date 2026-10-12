// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
namespace
{
tube5::HardwareClipStatus clip_status;
}
#ifdef TUBE5_HW_FRAME
#include "failure_log.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C"
{
    std::size_t sceKernelGetDirectMemorySize();
    int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int,
                                      std::int64_t *);
    int sceKernelMapDirectMemory(void **, std::size_t, int, int, std::int64_t, std::size_t);
    int sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
    int sceKernelMunmap(void *, std::size_t);
    int sceKernelMapNamedFlexibleMemory(void **, std::size_t, int, int, const char *);
    int sceKernelAvailableFlexibleMemorySize(std::size_t *);
    std::uint64_t sceKernelGetProcessTime();
    int sceVideodec2QueryComputeMemoryInfo(tube5::VideoComputeMemory *);
    int sceVideodec2AllocateComputeQueue(const tube5::VideoComputeConfig *,
                                         const tube5::VideoComputeMemory *, void **);
    int sceVideodec2ReleaseComputeQueue(void *);
    int sceVideodec2QueryDecoderMemoryInfo(const tube5::VideoDecoderConfig *,
                                           tube5::VideoDecoderMemory *);
    int sceVideodec2CreateDecoder(const tube5::VideoDecoderConfig *,
                                  const tube5::VideoDecoderMemory *, void **);
    int sceVideodec2DeleteDecoder(void *);
    int sceVideodec2Reset(void *);
    int sceVideodec2Decode(void *, tube5::VideoInput *, tube5::VideoFrame *, tube5::VideoOutput *);
    int sceVideodec2Flush(void *, tube5::VideoFrame *, tube5::VideoOutput *);
}
namespace
{
constexpr std::size_t page = 0x4000, mib = 1024 * 1024;
struct Direct
{
    void *data = nullptr;
    std::int64_t physical = 0;
    std::size_t bytes = 0;
};
struct Resources
{
    Direct compute, gpu, shared, input, frame;
    void *cpu = nullptr, *queue = nullptr, *decoder = nullptr;
    std::size_t cpu_bytes = 0;
    bool quarantined = false;
};
Resources resources; // Deliberately no destructor: failed hardware teardown is quarantined.
struct Clip
{
    int descriptor = -1;
    std::uint32_t offsets[300]{}, sizes[300]{};
    char log[256]{};
    const char *stage = "IDLE";
    const char *failure_stage = "NONE";
    std::uint64_t started = 0;
    std::uint64_t last_work_us = 0;
    std::size_t flexible_before = 0, flexible_after = 0;
    int before_rc = 0, after_rc = 0;
    unsigned first_sum = 0, last_sum = 0;
};
Clip clip;
void clip_snapshot()
{
    const auto &s = clip_status;
    char report[1800]{};
    const int length = std::snprintf(
        report, sizeof(report),
        "TUBE5 HARDWARE CLIP V1\nBUILD %s\n1080P AVC HIGH IP - CPU SDL DISPLAY\n"
        "STAGE %s FAILURE %s ERROR %08X ACTIVE %u CANCELLED %u\n"
        "FRAMES %u FPS %u SUBMITTED %u DECODED %u COMPOSED %u CHANGED %u SLOW %u\n"
        "DECODE MAX US %llu CONVERT MAX US %llu COPY MAX US %llu WORK MAX US %llu ELAPSED US %llu\n"
        "LUMA FIRST %u LAST %u\nCLEANUP %08X QUARANTINED %u\n"
        "FLEX BEFORE %llu RC %08X AFTER %llu RC %08X\n",
        tube5::failure_log_build_label(), clip.stage, clip.failure_stage,
        static_cast<unsigned>(s.error), s.active ? 1u : 0u, s.cancelled ? 1u : 0u, s.frames, s.fps,
        s.submitted, s.decoded, s.presented, s.changed_frames, s.slow_frames,
        static_cast<unsigned long long>(s.decode_max_us),
        static_cast<unsigned long long>(s.convert_max_us),
        static_cast<unsigned long long>(s.copy_max_us),
        static_cast<unsigned long long>(s.work_max_us),
        static_cast<unsigned long long>(s.elapsed_us), clip.first_sum, clip.last_sum,
        static_cast<unsigned>(s.cleanup_error), resources.quarantined ? 1u : 0u,
        static_cast<unsigned long long>(clip.flexible_before),
        static_cast<unsigned>(clip.before_rc), static_cast<unsigned long long>(clip.flexible_after),
        static_cast<unsigned>(clip.after_rc));
    if (length > 0 && clip.log[0])
        (void)tube5::write_failure_log(clip.log, report,
                                       std::min<std::size_t>(length, sizeof(report) - 1));
}
int read_exact(int descriptor, void *data, std::size_t bytes)
{
    auto *target = static_cast<unsigned char *>(data);
    while (bytes)
    {
        const auto count = ::read(descriptor, target, bytes);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -EIO;
        target += count;
        bytes -= static_cast<std::size_t>(count);
    }
    return 0;
}
std::uint32_t little32(const unsigned char *data)
{
    return data[0] | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}
std::size_t rounded(std::uint64_t value, std::size_t maximum)
{
    return value && value <= maximum ? (static_cast<std::size_t>(value) + page - 1) & ~(page - 1)
                                     : 0;
}
int allocate(Direct &buffer, std::size_t bytes, int protection)
{
    if (!bytes)
        return -EINVAL;
    const auto limit = sceKernelGetDirectMemorySize();
    if (limit > INT64_MAX || bytes > limit)
        return -ENOMEM;
    const int result = sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(limit), bytes,
                                                     page, 12, &buffer.physical);
    if (result)
        return result;
    buffer.bytes = bytes;
    const int mapped =
        sceKernelMapDirectMemory(&buffer.data, bytes, protection, 0, buffer.physical, page);
    return mapped ? mapped : (buffer.data ? 0 : -ENOMEM);
}
int release(Direct &buffer)
{
    if (buffer.data)
    {
        const int result = sceKernelMunmap(buffer.data, buffer.bytes);
        if (result)
            return result;
        buffer.data = nullptr;
    }
    if (buffer.bytes)
    {
        const int result = sceKernelReleaseDirectMemory(buffer.physical, buffer.bytes);
        if (result)
            return result;
        buffer.bytes = 0;
    }
    return 0;
}
int cleanup()
{
    auto &r = resources;
    if (r.quarantined)
        return -EBUSY; // An error returned an ambiguous hardware handle.
    int result = r.decoder ? sceVideodec2DeleteDecoder(r.decoder) : 0;
    if (result)
        return result; // Hardware might still reference every allocation.
    r.decoder = nullptr;
    result = r.queue ? sceVideodec2ReleaseComputeQueue(r.queue) : 0;
    if (result)
        return result;
    r.queue = nullptr;
    for (auto *buffer : {&r.frame, &r.input, &r.shared, &r.gpu, &r.compute})
    {
        result = release(*buffer);
        if (result)
            return result;
    }
    if (r.cpu)
    {
        result = sceKernelMunmap(r.cpu, r.cpu_bytes); // Releases the flexible VM mapping.
        if (result)
            return result;
        r.cpu = nullptr;
    }
    return 0;
}
template <class Recorder> int prepare_decoder(Recorder record)
{
    auto &r = resources;
    tube5::VideoComputeMemory compute{};
    compute.size = sizeof(compute);
    int code = sceVideodec2QueryComputeMemoryInfo(&compute);
    record("COMPUTE QUERY", code);
    if (code)
        return code;
    code = allocate(r.compute, rounded(compute.bytes, 16 * mib), 0x33);
    record("COMPUTE MEMORY", code);
    if (code)
        return code;
    compute.shared = r.compute.data;
    compute.bytes = r.compute.bytes;
    tube5::VideoComputeConfig queue{};
    queue.size = sizeof(queue);
    code = sceVideodec2AllocateComputeQueue(&queue, &compute, &r.queue);
    record("COMPUTE CREATE", code);
    if (code && r.queue)
        r.quarantined = true;
    if (code || !r.queue)
        return code ? code : -EINVAL;
    tube5::VideoDecoderConfig config{};
    config.size = sizeof(config);
    config.resource = 1;
    config.codec = 1;
    config.profile = 100;
    config.level = 51;
    config.width = 1920;
    config.height = 1088;
    config.dpb = 4;
    config.depth = 1;
    config.queue = reinterpret_cast<std::uintptr_t>(r.queue);
    config.affinity = 0x3f;
    config.priority = 700;
    config.progressive = 1;
    tube5::VideoDecoderMemory memory{};
    memory.size = sizeof(memory);
    code = sceVideodec2QueryDecoderMemoryInfo(&config, &memory);
    record("DECODER QUERY", code);
    if (code)
        return code;
    r.cpu_bytes = rounded(memory.cpu_bytes, 64 * mib);
    const auto gpu_bytes = rounded(memory.gpu_bytes, 128 * mib);
    const auto shared_bytes = rounded(memory.shared_bytes, 64 * mib);
    const auto frame_bytes = rounded(memory.frame_bytes, 16 * mib);
    if (!r.cpu_bytes || !gpu_bytes || (memory.shared_bytes && !shared_bytes) || !frame_bytes ||
        !memory.alignment || memory.alignment > page ||
        (memory.alignment & (memory.alignment - 1)) ||
        r.cpu_bytes + gpu_bytes + shared_bytes + frame_bytes + r.compute.bytes + mib > 256 * mib)
        return -EINVAL;
    code = sceKernelMapNamedFlexibleMemory(&r.cpu, r.cpu_bytes, 3, 0, "Tube5HwCpu");
    record("CPU MEMORY", code);
    if (code || !r.cpu)
        return code ? code : -ENOMEM;
    memory.cpu = r.cpu;
    code = allocate(r.gpu, gpu_bytes, 0x32);
    record("GPU MEMORY", code);
    if (code)
        return code;
    memory.gpu = r.gpu.data;
    memory.gpu_bytes = r.gpu.bytes;
    if (shared_bytes)
    {
        code = allocate(r.shared, shared_bytes, 0x33);
        if (code)
            return code;
        memory.shared = r.shared.data;
        memory.shared_bytes = r.shared.bytes;
    }
    record("DECODER CREATE BEGIN", 0);
    code = sceVideodec2CreateDecoder(&config, &memory, &r.decoder);
    record("DECODER CREATE", code);
    if (code && r.decoder)
        r.quarantined = true;
    if (code || !r.decoder)
        return code ? code : -EINVAL;
    code = allocate(r.input, mib, 0x32);
    if (!code)
        code = allocate(r.frame, frame_bytes, 0x33);
    record("FRAME MEMORY", code);
    return code;
}
} // namespace
#endif
namespace tube5
{
int hardware_frame_probe(const char *asset, const char *log, HardwareFrameConsumer consumer,
                         void *consumer_context) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)asset;
    (void)log;
    (void)consumer;
    (void)consumer_context;
    return -1;
#else
    auto &r = resources;
    if (r.quarantined || clip_status.active)
        return -EBUSY;
    char report[3072]{};
    std::size_t used = static_cast<std::size_t>(std::snprintf(
        report, sizeof(report), "TUBE5 HARDWARE FRAME V1\nBUILD %s\n1080P AVC IDR - %s\n",
        failure_log_build_label(), consumer ? "DISPLAY CONSUMER" : "NO DISPLAY TEST"));
    const auto record = [&](const char *stage, int code)
    {
        const int count = std::snprintf(report + used, sizeof(report) - used, "%s %08X\n", stage,
                                        static_cast<unsigned>(code));
        if (count > 0)
            used += std::min<std::size_t>(count, sizeof(report) - used - 1);
        (void)write_failure_log(log, report, used);
    };
    int fd = -1;
    int result = 0;
    const auto run = [&]() -> int
    {
        fd = ::open(asset, O_RDONLY | O_NOFOLLOW);
        struct stat metadata{};
        if (fd < 0 || fstat(fd, &metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size <= 0 ||
            metadata.st_size > static_cast<off_t>(mib))
            return -EINVAL;
        int code = prepare_decoder(record);
        if (code)
            return code;
        std::size_t loaded = 0;
        while (loaded < static_cast<std::size_t>(metadata.st_size))
        {
            const auto count =
                ::read(fd, static_cast<char *>(r.input.data) + loaded, metadata.st_size - loaded);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                return -EIO;
            loaded += static_cast<std::size_t>(count);
        }
        std::memset(r.frame.data, 0xcd, r.frame.bytes);
        code = sceVideodec2Reset(r.decoder);
        record("RESET", code);
        if (code)
            return code;
        VideoInput input{sizeof(VideoInput), r.input.data, loaded, 0, UINT64_MAX, 0};
        VideoFrame frame{sizeof(VideoFrame), r.frame.data, r.frame.bytes, 0, 0};
        VideoOutput output{};
        output.size = sizeof(output);
        record("DECODE BEGIN", 0);
        code = sceVideodec2Decode(r.decoder, &input, &frame, &output);
        record("DECODE", code);
        if (code || output.error)
            return code ? code : -EIO;
        if (!output.valid)
        {
            output = {};
            output.size = sizeof(output);
            record("FLUSH BEGIN", 0);
            code = sceVideodec2Flush(r.decoder, &frame, &output);
            record("FLUSH", code);
        }
        const bool owned = output.buffer == r.frame.data;
        const int count =
            std::snprintf(report + used, sizeof(report) - used,
                          "OUTPUT VALID %u ERROR %u PICTURES %u CODEC %u W %u H %u PITCH %u "
                          "BYTEPITCH %u FORMAT %u OWNED %u BYTES %llu\n",
                          output.valid, output.error, output.pictures, output.codec, output.width,
                          output.height, output.pitch, output.pitch_bytes, output.format,
                          owned ? 1u : 0u, static_cast<unsigned long long>(output.bytes));
        if (count > 0)
            used += std::min<std::size_t>(count, sizeof(report) - used - 1);
        if (code || !output.valid || output.error || output.codec != 1 || !owned ||
            output.bytes > r.frame.bytes || output.width != 1920 || output.height < 1080 ||
            output.height > 1088 || output.pitch_bytes < 1920 || output.pitch_bytes > 4096 ||
            static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2 > r.frame.bytes)
            return code ? code : -EINVAL;
        unsigned changed = 0, samples = 0, sum = 0, minimum = 255, maximum = 0;
        for (unsigned y = 0; y < 1080; y += 31)
            for (unsigned x = 0; x < 1920; x += 31)
            {
                const unsigned value =
                    static_cast<unsigned char *>(output.buffer)[y * output.pitch_bytes + x];
                changed += value != 0xcd;
                ++samples;
                sum += value;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        const int sample_length =
            std::snprintf(report + used, sizeof(report) - used,
                          "LUMA SAMPLES %u SUM %u MIN %u MAX %u\n", samples, sum, minimum, maximum);
        if (sample_length > 0)
            used += std::min<std::size_t>(sample_length, sizeof(report) - used - 1);
        record("CHANGED LUMA SAMPLES", static_cast<int>(changed));
        if (changed)
        {
            const int consumed = consumer ? consumer(output, consumer_context) : 0;
            record("FRAME CONSUMER", consumed);
            if (consumed)
                return consumed;
            return 0;
        }
        return -EIO;
    };
    record("BEGIN", 0);
    std::size_t flexible_before = 0, flexible_after = 0;
    const int before_query = sceKernelAvailableFlexibleMemorySize(&flexible_before);
    result = run();
    if (fd >= 0)
        (void)::close(fd);
    record("RESULT", result);
    record("CLEANUP BEGIN", 0);
    const int retired = cleanup();
    r.quarantined = retired != 0;
    record("CLEANUP", retired);
    record("QUARANTINED", r.quarantined ? 1 : 0);
    const int after_query = sceKernelAvailableFlexibleMemorySize(&flexible_after);
    const int memory_length = std::snprintf(
        report + used, sizeof(report) - used, "FLEX BEFORE %llu RC %08X AFTER %llu RC %08X\n",
        static_cast<unsigned long long>(flexible_before), static_cast<unsigned>(before_query),
        static_cast<unsigned long long>(flexible_after), static_cast<unsigned>(after_query));
    if (memory_length > 0)
        used += std::min<std::size_t>(memory_length, sizeof(report) - used - 1);
    (void)write_failure_log(log, report, used);
    return result ? result : retired;
#endif
}
int hardware_frame_cycles(const char *asset, const char *directory) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)asset;
    (void)directory;
    return -1;
#else
    if (!asset || !directory)
        return -EINVAL;
    char summary[2048]{}, summary_path[512]{}, cycle_path[512]{};
    int length = std::snprintf(summary_path, sizeof(summary_path), "%s/tube5-hardware-cycles.log",
                               directory);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(summary_path))
        return -EINVAL;
    std::size_t used = static_cast<std::size_t>(
        std::snprintf(summary, sizeof(summary),
                      "TUBE5 HARDWARE CYCLES V1\nBUILD %s\nCYCLES 8 - NO DISPLAY TEST\n",
                      failure_log_build_label()));
    for (unsigned cycle = 1; cycle <= 8; ++cycle)
    {
        length = std::snprintf(cycle_path, sizeof(cycle_path), "%s/tube5-hardware-cycle-%u.log",
                               directory, cycle);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(cycle_path))
            return -EINVAL;
        std::size_t before = 0, after = 0;
        const int before_code = sceKernelAvailableFlexibleMemorySize(&before);
        length = std::snprintf(
            summary + used, sizeof(summary) - used, "CYCLE %u BEGIN FLEX %llu RC %08X\n", cycle,
            static_cast<unsigned long long>(before), static_cast<unsigned>(before_code));
        if (length > 0)
            used += std::min<std::size_t>(length, sizeof(summary) - used - 1);
        if (const int saved = write_failure_log(summary_path, summary, used))
            return saved; // Do not start hardware work without a progress snapshot.
        const int result = hardware_frame_probe(asset, cycle_path);
        const int after_code = sceKernelAvailableFlexibleMemorySize(&after);
        length = std::snprintf(
            summary + used, sizeof(summary) - used, "CYCLE %u RESULT %08X FLEX %llu RC %08X\n",
            cycle, static_cast<unsigned>(result), static_cast<unsigned long long>(after),
            static_cast<unsigned>(after_code));
        if (length > 0)
            used += std::min<std::size_t>(length, sizeof(summary) - used - 1);
        const int saved = write_failure_log(summary_path, summary, used);
        if (result || saved)
            return result ? result : saved;
    }
    return 0;
#endif
}
const HardwareClipStatus &hardware_clip_status() noexcept
{
    return clip_status;
}
int hardware_clip_close(bool cancelled, int display_error) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)cancelled;
    (void)display_error;
    return -1;
#else
    if (!clip_status.active)
        return clip_status.cleanup_error;
    if (display_error)
    {
        clip_status.error = display_error;
        clip.failure_stage = "DISPLAY";
    }
    clip_status.cancelled = cancelled;
    if (clip.descriptor >= 0)
        (void)::close(clip.descriptor);
    clip.descriptor = -1;
    clip.stage = "CLEANUP BEGIN";
    clip_snapshot();
    clip_status.cleanup_error = cleanup();
    resources.quarantined = clip_status.cleanup_error != 0;
    clip_status.active = false;
    if (clip.started)
        clip_status.elapsed_us = sceKernelGetProcessTime() - clip.started;
    clip.after_rc = sceKernelAvailableFlexibleMemorySize(&clip.flexible_after);
    clip.stage = "CLOSED";
    clip_snapshot();
    return clip_status.cleanup_error;
#endif
}
int hardware_clip_begin(const char *asset, const char *log) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)asset;
    (void)log;
    return -1;
#else
    if (clip_status.active || resources.quarantined || resources.decoder || resources.queue)
        return -EBUSY;
    if (!asset || !log || std::strlen(log) >= sizeof(clip.log))
        return -EINVAL;
    clip = {};
    clip_status = {};
    clip_status.active = true; // Includes partial setup so every error uses the same cleanup.
    std::strcpy(clip.log, log);
    clip.before_rc = sceKernelAvailableFlexibleMemorySize(&clip.flexible_before);
    const auto fail = [](int code)
    {
        clip_status.error = code;
        clip.failure_stage = clip.stage;
        hardware_clip_close();
        return code;
    };
    clip.stage = "OPEN";
    clip_snapshot();
    clip.descriptor = ::open(asset, O_RDONLY | O_NOFOLLOW);
    struct stat metadata{};
    unsigned char header[28]{};
    if (clip.descriptor < 0 || fstat(clip.descriptor, &metadata) || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 28 || metadata.st_size > static_cast<off_t>(16 * mib) ||
        read_exact(clip.descriptor, header, sizeof(header)))
        return fail(-EINVAL);
    clip_status.fps = little32(header + 16);
    clip_status.frames = little32(header + 20);
    if (std::memcmp(header, "T5HC", 4) || little32(header + 4) != 1 ||
        little32(header + 8) != 1920 || little32(header + 12) != 1080 || clip_status.fps != 30 ||
        !clip_status.frames || clip_status.frames > 300 || little32(header + 24))
        return fail(-EINVAL);
    std::uint64_t offset = sizeof(header);
    for (unsigned i = 0; i < clip_status.frames; ++i)
    {
        unsigned char size[4]{};
        if (offset + 4 > static_cast<std::uint64_t>(metadata.st_size) ||
            ::lseek(clip.descriptor, static_cast<off_t>(offset), SEEK_SET) < 0 ||
            read_exact(clip.descriptor, size, sizeof(size)))
            return fail(-EINVAL);
        offset += 4;
        clip.sizes[i] = little32(size);
        clip.offsets[i] = static_cast<std::uint32_t>(offset);
        if (!clip.sizes[i] || clip.sizes[i] >= mib ||
            offset + clip.sizes[i] > static_cast<std::uint64_t>(metadata.st_size))
            return fail(-EINVAL);
        offset += clip.sizes[i];
    }
    if (offset != static_cast<std::uint64_t>(metadata.st_size))
        return fail(-EINVAL);
    const auto record = [](const char *stage, int code)
    {
        clip.stage = stage;
        clip_status.error = code;
        clip_snapshot();
    };
    int code = prepare_decoder(record);
    if (code)
        return fail(code);
    code = sceVideodec2Reset(resources.decoder);
    record("RESET", code);
    if (code)
        return fail(code);
    clip.stage = "READY";
    clip_snapshot();
    return 0;
#endif
}
int hardware_clip_step(HardwareFrameConsumer consumer, void *context) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)consumer;
    (void)context;
    return -1;
#else
    auto &s = clip_status;
    auto &r = resources;
    if (!s.active || !consumer)
        return -EINVAL;
    if (s.error)
        return s.error;
    if (s.submitted == s.frames)
        return 1;
    const auto fail = [](const char *stage, int code)
    {
        clip.stage = stage;
        clip.failure_stage = stage;
        clip_status.error = code;
        clip_snapshot();
        return code;
    };
    if (!clip.started)
        clip.started = sceKernelGetProcessTime();
    const unsigned index = s.submitted;
    if (::lseek(clip.descriptor, clip.offsets[index], SEEK_SET) < 0 ||
        read_exact(clip.descriptor, r.input.data, clip.sizes[index]))
        return fail("READ", -EIO);
    VideoInput input{sizeof(VideoInput), r.input.data,
                     clip.sizes[index],  static_cast<std::uint64_t>(index) * 1000000 / s.fps,
                     UINT64_MAX,         0};
    VideoFrame frame{sizeof(VideoFrame), r.frame.data, r.frame.bytes, 0, 0};
    VideoOutput output{};
    output.size = sizeof(output);
    clip.stage = "DECODE";
    if (index % 30 == 0)
        clip_snapshot();
    const auto decode_start = sceKernelGetProcessTime();
    int code = sceVideodec2Decode(r.decoder, &input, &frame, &output);
    ++s.submitted;
    if (code || output.error)
        return fail("DECODE", code ? code : -EIO);
    if (!output.valid)
    {
        output = {};
        output.size = sizeof(output);
        code = sceVideodec2Flush(r.decoder, &frame, &output);
    }
    const auto decode_us = sceKernelGetProcessTime() - decode_start;
    s.decode_max_us = std::max(s.decode_max_us, decode_us);
    const auto required = static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2;
    if (code || output.error || !output.valid || output.pictures != 1 || output.codec != 1 ||
        output.format != 0 || output.buffer != r.frame.data || output.width != 1920 ||
        output.height < 1080 || output.height > 1088 || (output.height & 1u) ||
        output.pitch_bytes < 1920 || output.pitch_bytes > 4096 || (output.pitch_bytes & 1u) ||
        output.bytes > r.frame.bytes || required > output.bytes)
        return fail("OUTPUT", code ? code : -EINVAL);
    unsigned sum = 0;
    for (unsigned y = 0; y < 1080; y += 31)
        for (unsigned x = 0; x < 1920; x += 31)
            sum += static_cast<unsigned char *>(output.buffer)[y * output.pitch_bytes + x];
    if (!s.decoded)
        clip.first_sum = sum;
    else if (sum != clip.last_sum)
        ++s.changed_frames;
    clip.last_sum = sum;
    ++s.decoded;
    const auto convert_start = sceKernelGetProcessTime();
    code = consumer(output, context);
    const auto convert_us = sceKernelGetProcessTime() - convert_start;
    s.convert_max_us = std::max(s.convert_max_us, convert_us);
    clip.last_work_us = decode_us + convert_us;
    if (code)
        return fail("CONSUMER", code);
    s.elapsed_us = sceKernelGetProcessTime() - clip.started;
    clip.stage = "FRAME COPIED";
    if (s.submitted % 30 == 0 || s.submitted == s.frames)
        clip_snapshot();
    return 0;
#endif
}
void hardware_clip_presented(std::uint64_t copy_us) noexcept
{
#ifdef TUBE5_HW_FRAME
    if (clip_status.active && clip_status.presented < clip_status.decoded)
    {
        ++clip_status.presented;
        clip_status.copy_max_us = std::max(clip_status.copy_max_us, copy_us);
        const auto work_us = clip.last_work_us + copy_us;
        clip_status.work_max_us = std::max(clip_status.work_max_us, work_us);
        clip_status.slow_frames += work_us > 1000000 / clip_status.fps;
    }
#else
    (void)copy_us;
#endif
}
} // namespace tube5
