// SPDX-License-Identifier: GPL-3.0-or-later
#include "hardware_frame_probe.hpp"
namespace
{
tube5::HardwareClipStatus clip_status;
tube5::HardwareStreamStatus stream_status;
} // namespace
#ifdef TUBE5_HW_FRAME
#include "failure_log.hpp"
#include "vp9_packet.hpp"
#include "vp9_output_policy.hpp"
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
    int sceVideodec2GetPictureInfo(const tube5::VideoOutput *, tube5::VideoPictureInfo *, void *);
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
    bool vp9 = false, compound_vp9 = false, pipeline = false;
    unsigned width = 1920, height = 1080;
    unsigned slots = 1;
    int descriptor = -1;
    std::uint32_t offsets[300]{}, sizes[300]{};
    std::uint32_t pts_index[300]{}, crc[300]{};
    bool frame_used[6]{};
    unsigned input_tag[6]{};
    unsigned input_packet[6]{};
    bool input_displayable[6]{};
    bool input_retained[6]{};
    unsigned hidden_materialized = 0;
    int native_info_code = -ENOTSUP;
    bool native_info_guard = false;
    tube5::VideoPictureInfo native_info{};
    unsigned output_padding = 0, output_offered = 0, output_returned = 0;
    unsigned part_index = 0, next_coded_tag = 1;
    std::size_t frame_stride = 0;
    char log[256]{};
    const char *stage = "IDLE";
    const char *failure_stage = "NONE";
    std::uint64_t started = 0;
    std::uint64_t last_work_us = 0;
    std::size_t flexible_before = 0, flexible_after = 0;
    int before_rc = 0, after_rc = 0;
    unsigned first_sum = 0, last_sum = 0;
    std::uint32_t last_crc = 0, expected_crc = 0;
};
Clip clip;
struct Stream
{
    bool vp9 = false, pipeline = false;
    unsigned width = 1920, height = 1080;
    unsigned slots = 1;
    unsigned input_slots = 1;
    unsigned sync_input = 0;
    unsigned drain_attempts = 0;
    bool displayable = true;
    bool frame_used[6]{};
    struct Input
    {
        std::uint64_t tag = 0, pts = 0, dts = 0;
        bool displayable = true, retained = false;
    } input[6];
    std::uint64_t next_tag = 1;
    tube5::VideoOutput output{};
    tube5::VideoPictureInfo picture{};
    unsigned returned = 0;
    bool ready = false, borrowed = false, draining = false, eof = false;
};
Stream stream;
void clip_snapshot()
{
#ifdef TUBE5_HW_TIMING
    const auto log_start = sceKernelGetProcessTime();
#endif
    const auto &s = clip_status;
    char report[1800]{};
    const int length = std::snprintf(
        report, sizeof(report),
        "TUBE5 HARDWARE CLIP V1\nBUILD %s\n%s - DISPLAY CONSUMER\n"
        "STAGE %s FAILURE %s ERROR %08X ACTIVE %u CANCELLED %u\n"
        "FRAMES %u FPS %u SUBMITTED %u DECODED %u COMPOSED %u CHANGED %u SLOW %u\n"
        "DECODE MAX US %llu CONVERT MAX US %llu COPY MAX US %llu WORK MAX US %llu ELAPSED US %llu\n"
        "LUMA FIRST %u LAST %u\nCLEANUP %08X QUARANTINED %u\n"
        "NO OUTPUT %u DRAIN %u METADATA %u CRC MATCH %u INPUT REORDERS %u ORDER ERR %u\n"
        "PTS %llu DTS %llu ATTACHED %llu CRC LAST %08X EXPECTED %08X\n"
        "VP9 CODED %u HIDDEN %u SUPERFRAMES %u EXISTING %u\n"
        "VP9 HIDDEN NO OUTPUT %u LAST VALID %u ACCEPTED %u PICTURES %u\n"
        "PIPELINE HIDDEN MATERIALIZED %u OMITTED %u - FLAG POLICY CRC VERIFIER\n"
        "PROBE OUTPUT PADDING %u OFFERED %u RETURNED %u\n"
        "VP9 NATIVE INFO RC %d GUARD %u SIZE %llu VALID %u PTS %llu DTS %llu TAG %llu\n"
        "MEMORY REQUIRED CPU %llu GPU %llu SHARED %llu FRAME %llu\n"
        "FLEX BEFORE %llu RC %08X AFTER %llu RC %08X\n",
        tube5::failure_log_build_label(),
        clip.vp9    ? (clip.height == 2160 ? "2160P VP9 PROFILE 0 VISIBLE FRAMES"
                                           : "1440P VP9 PROFILE 0 VISIBLE FRAMES")
        : s.bframes ? "1080P AVC HIGH IPB"
                    : "1080P AVC HIGH IP",
        clip.stage, clip.failure_stage, static_cast<unsigned>(s.error), s.active ? 1u : 0u,
        s.cancelled ? 1u : 0u, s.frames, s.fps, s.submitted, s.decoded, s.presented,
        s.changed_frames, s.slow_frames, static_cast<unsigned long long>(s.decode_max_us),
        static_cast<unsigned long long>(s.convert_max_us),
        static_cast<unsigned long long>(s.copy_max_us),
        static_cast<unsigned long long>(s.work_max_us),
        static_cast<unsigned long long>(s.elapsed_us), clip.first_sum, clip.last_sum,
        static_cast<unsigned>(s.cleanup_error), resources.quarantined ? 1u : 0u, s.no_output,
        s.drain_calls, s.metadata, s.crc_matches, s.input_reorders, s.order_errors,
        static_cast<unsigned long long>(s.last_pts), static_cast<unsigned long long>(s.last_dts),
        static_cast<unsigned long long>(s.last_attached), clip.last_crc, clip.expected_crc,
        s.coded_frames, s.hidden_frames, s.superframes, s.existing_frames, s.hidden_no_output,
        s.last_valid, s.last_accepted, s.last_output_pictures,
        clip.hidden_materialized, s.hidden_no_output,
        clip.output_padding, clip.output_offered, clip.output_returned,
        clip.native_info_code, clip.native_info_guard ? 1u : 0u,
        static_cast<unsigned long long>(clip.native_info.size), clip.native_info.valid,
        static_cast<unsigned long long>(clip.native_info.pts),
        static_cast<unsigned long long>(clip.native_info.dts),
        static_cast<unsigned long long>(clip.native_info.attached),
        static_cast<unsigned long long>(s.required_cpu),
        static_cast<unsigned long long>(s.required_gpu),
        static_cast<unsigned long long>(s.required_shared),
        static_cast<unsigned long long>(s.required_frame),
        static_cast<unsigned long long>(clip.flexible_before),
        static_cast<unsigned>(clip.before_rc), static_cast<unsigned long long>(clip.flexible_after),
        static_cast<unsigned>(clip.after_rc));
    if (length > 0 && clip.log[0])
        (void)tube5::write_failure_log(clip.log, report,
                                       std::min<std::size_t>(length, sizeof(report) - 1));
#ifdef TUBE5_HW_TIMING
    clip_status.last_log_us += sceKernelGetProcessTime() - log_start;
#endif
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
template <class Recorder>
int prepare_decoder(Recorder record, unsigned depth = 1, unsigned slots = 1,
                    const tube5::VideoDecoderConfig *requested = nullptr,
                    unsigned input_slots = 0)
{
    if (!input_slots)
        input_slots = slots;
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
    if (requested)
    {
        config.codec = requested->codec;
        config.profile = requested->profile;
        config.level = requested->level;
        config.width = requested->width;
        config.height = requested->height;
    }
    config.dpb = 4;
    config.depth = depth;
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
    if (clip_status.active)
    {
        clip_status.required_cpu = memory.cpu_bytes;
        clip_status.required_gpu = memory.gpu_bytes;
        clip_status.required_shared = memory.shared_bytes;
        clip_status.required_frame = memory.frame_bytes;
    }
    record("DECODER CPU BYTES", static_cast<int>(memory.cpu_bytes));
    record("DECODER GPU BYTES", static_cast<int>(memory.gpu_bytes));
    record("DECODER SHARED BYTES", static_cast<int>(memory.shared_bytes));
    record("DECODER FRAME BYTES", static_cast<int>(memory.frame_bytes));
    record("DECODER ALIGNMENT", static_cast<int>(memory.alignment));
    r.cpu_bytes = rounded(memory.cpu_bytes, 64 * mib);
    const bool vp9_4k =
        config.codec == 0x00245bfdu && config.width == 3840 && config.height == 2160;
    const auto gpu_bytes = rounded(memory.gpu_bytes, (vp9_4k ? 256 : 128) * mib);
    // VP9 1440p on FW 13.20 requests ~101 MiB here; retain the total budget below.
    const auto shared_bytes = rounded(memory.shared_bytes, (vp9_4k ? 256 : 128) * mib);
    const auto frame_bytes = rounded(memory.frame_bytes, 16 * mib);
    if (!r.cpu_bytes || !gpu_bytes || (memory.shared_bytes && !shared_bytes) || !frame_bytes ||
        !memory.alignment || memory.alignment > page ||
        (memory.alignment & (memory.alignment - 1)) ||
        r.cpu_bytes + gpu_bytes + shared_bytes + frame_bytes * slots + r.compute.bytes +
                mib * input_slots >
            (vp9_4k ? 512 : 256) * mib ||
        !slots || slots > 6 || input_slots > 6)
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
    clip.frame_stride = frame_bytes;
    code = allocate(r.input, mib * input_slots, 0x32);
    if (!code)
        code = allocate(r.frame, frame_bytes * slots, 0x33);
    record("FRAME MEMORY", code);
    return code;
}
} // namespace
#endif
namespace tube5
{
int hardware_frame_probe(const char *asset, const char *log, HardwareFrameConsumer consumer,
                         void *consumer_context, HardwareFrameMode mode) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)asset;
    (void)log;
    (void)consumer;
    (void)consumer_context;
    (void)mode;
    return -1;
#else
    auto &r = resources;
    if (r.quarantined || clip_status.active || stream_status.active)
        return -EBUSY;
    const bool vp9 = mode != HardwareFrameMode::avc1080;
    const bool four_k = mode == HardwareFrameMode::vp9_2160;
    const unsigned width = four_k ? 3840u
                           : vp9  ? 2560u
                                  : 1920u,
                   height = four_k ? 2160u
                            : vp9  ? 1440u
                                   : 1080u;
    VideoDecoderConfig requested{};
    requested.codec = vp9 ? 0x00245bfdu : 1u;
    requested.profile = vp9 ? 0u : 100u;
    requested.level = four_k ? 51u : vp9 ? 50u : 51u;
    requested.width = width;
    requested.height = vp9 ? height : 1088u;
    char report[3072]{};
    std::size_t used = static_cast<std::size_t>(std::snprintf(
        report, sizeof(report), "TUBE5 HARDWARE FRAME V2\nBUILD %s\n%ux%u %s KEYFRAME - %s\n",
        failure_log_build_label(), width, height, vp9 ? "VP9 PROFILE 0" : "AVC HIGH",
        consumer ? "DISPLAY CONSUMER" : "NO DISPLAY TEST"));
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
        int code = prepare_decoder(record, 1, 1, &requested);
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
        if (code || !output.valid || output.error || output.codec != requested.codec || !owned ||
            output.format != 0 || output.pictures != 1 || (output.height & 1u) ||
            output.bytes > r.frame.bytes || output.width != width || output.height < height ||
            output.height > static_cast<unsigned>(requested.height) || output.pitch_bytes < width ||
            output.pitch_bytes > 4096 ||
            static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2 > r.frame.bytes)
            return code ? code : -EINVAL;
        unsigned changed = 0, samples = 0, sum = 0, minimum = 255, maximum = 0;
        for (unsigned y = 0; y < height; y += 31)
            for (unsigned x = 0; x < width; x += 31)
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
    if (clip_status.active || stream_status.active || resources.quarantined || resources.decoder ||
        resources.queue)
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
        metadata.st_size < 28 || metadata.st_size > static_cast<off_t>(32 * mib) ||
        read_exact(clip.descriptor, header, sizeof(header)))
        return fail(-EINVAL);
    clip_status.fps = little32(header + 16);
    clip_status.frames = little32(header + 20);
    const auto version = little32(header + 4);
    clip_status.bframes = version == 2;
    clip.vp9 = version >= 3 && version <= 7;
    clip.compound_vp9 = version == 4 || version == 7;
#ifdef TUBE5_VP9_DEEP_PROBE
    clip.pipeline = version == 6 || version == 7; // Diagnostic fixtures, not production.
#endif
    clip.width = version >= 5 ? 3840 : clip.vp9 ? 2560 : 1920;
    clip.height = version >= 5 ? 2160 : clip.vp9 ? 1440 : 1080;
    if (std::memcmp(header, "T5HC", 4) || (version < 1 || version > 7) ||
        (version < 6 && metadata.st_size > static_cast<off_t>(16 * mib)) ||
        little32(header + 8) != clip.width || little32(header + 12) != clip.height ||
        clip_status.fps != (version >= 6 ? 60u : 30u) || !clip_status.frames ||
        clip_status.frames > 300 ||
        little32(header + 24) != (clip.vp9              ? 2u
                                  : clip_status.bframes ? 1u
                                                        : 0u))
        return fail(-EINVAL);
    std::uint64_t offset = sizeof(header);
    bool seen_pts[300]{};
    for (unsigned i = 0; i < clip_status.frames; ++i)
    {
        unsigned char entry[16]{};
        const unsigned entry_bytes = (clip_status.bframes || clip.vp9) ? 16 : 4;
        if (offset + entry_bytes > static_cast<std::uint64_t>(metadata.st_size) ||
            ::lseek(clip.descriptor, static_cast<off_t>(offset), SEEK_SET) < 0 ||
            read_exact(clip.descriptor, entry, entry_bytes))
            return fail(-EINVAL);
        offset += entry_bytes;
        clip.sizes[i] = little32(entry);
        clip.pts_index[i] = clip_status.bframes ? little32(entry + 4) : i;
        clip.crc[i] = (clip_status.bframes || clip.vp9) ? little32(entry + 12) : 0;
        if (clip.pts_index[i] >= clip_status.frames || seen_pts[clip.pts_index[i]] ||
            ((clip_status.bframes || clip.vp9) && little32(entry + 8) != i) ||
            (clip.vp9 && little32(entry + 4) != i))
            return fail(-EINVAL);
        seen_pts[clip.pts_index[i]] = true;
        clip_status.input_reorders += clip.pts_index[i] != i;
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
    VideoDecoderConfig requested{};
    requested.codec = 0x00245bfd;
    requested.profile = 0;
    requested.level = clip.width == 3840 ? 51 : 50;
    requested.width = clip.width;
    requested.height = clip.height;
    const bool pipeline = clip_status.bframes || clip.pipeline;
    clip.slots = clip.pipeline ? 4 : clip_status.bframes ? 6 : 1;
    int code = prepare_decoder(record, pipeline ? 3 : 1, clip.slots,
                               clip.vp9 ? &requested : nullptr);
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
#ifdef TUBE5_HW_FRAME
static int hardware_bclip_step(HardwareFrameConsumer consumer, void *context) noexcept
{
    auto &s = clip_status;
    auto &r = resources;
    const auto fail = [](const char *stage, int code)
    {
        clip.stage = clip.failure_stage = stage;
        clip_status.error = code;
        clip_snapshot();
        return code;
    };
    if (s.decoded == s.frames)
        return s.submitted == s.frames ? 1 : fail("EARLY EOF", -EINVAL);
    const auto step_start = sceKernelGetProcessTime();
    s.last_read_us = s.last_crc_us = s.last_log_us = 0;
    if (!clip.started)
        clip.started = sceKernelGetProcessTime();
    unsigned offered = 0;
    while (offered < clip.slots && clip.frame_used[offered])
        ++offered;
    if (offered == clip.slots)
        return fail("FRAME POOL", -ENOBUFS);
    auto *offered_buffer = static_cast<unsigned char *>(r.frame.data) + offered * clip.frame_stride;
    VideoFrame frame{sizeof(VideoFrame), offered_buffer, clip.frame_stride, 0, 0};
    VideoOutput output{};
    output.size = sizeof(output);
    int code = 0;
    unsigned submitted_slot = clip.slots;
    std::uint64_t decode_start = 0;
    if (s.submitted < s.frames)
    {
        unsigned input_slot = 0;
        while (input_slot < clip.slots && clip.input_tag[input_slot])
            ++input_slot;
        if (input_slot == clip.slots)
            return fail("INPUT POOL", -ENOBUFS);
        const unsigned index = s.submitted;
        auto *data = static_cast<unsigned char *>(r.input.data) + input_slot * mib;
        if (::lseek(clip.descriptor, clip.offsets[index], SEEK_SET) < 0 ||
            read_exact(clip.descriptor, data, clip.sizes[index]))
            return fail("READ", -EIO);
        s.last_read_us = sceKernelGetProcessTime() - step_start;
        const std::uint8_t *au_data = data;
        std::size_t au_bytes = clip.sizes[index];
        bool displayable = true;
        unsigned part_count = 1;
        if (clip.vp9)
        {
            Vp9Packet packet{};
            if (!split_vp9_packet(data, clip.sizes[index], packet) ||
                clip.part_index >= packet.count ||
                (!clip.compound_vp9 && packet.count != 1))
                return fail("PIPELINE FIXTURE LAYOUT", -EINVAL);
            unsigned visible = 0;
            for (unsigned i = 0; i < packet.count; ++i)
                visible += packet.parts[i].displayable;
            if (visible != 1 || !packet.parts[packet.count - 1].displayable)
                return fail("PIPELINE FIXTURE DISPLAY", -EINVAL);
            const auto &part = packet.parts[clip.part_index];
            au_data = part.data;
            au_bytes = part.bytes;
            displayable = part.displayable;
            part_count = packet.count;
            if (!clip.part_index)
                s.superframes += packet.count > 1;
            s.hidden_frames += !displayable;
            s.existing_frames += part.existing;
            ++s.coded_frames;
        }
        const unsigned tag = clip.vp9 ? clip.next_coded_tag++ : index + 1;
        clip.input_tag[input_slot] = tag;
        clip.input_packet[input_slot] = index;
        clip.input_displayable[input_slot] = displayable;
        clip.input_retained[input_slot] = false;
        submitted_slot = input_slot;
        VideoInput input{sizeof(VideoInput),
                         const_cast<std::uint8_t *>(au_data),
                         au_bytes,
                         static_cast<std::uint64_t>(clip.pts_index[index]) * 1000000 / s.fps,
                         static_cast<std::uint64_t>(index) * 1000000 / s.fps,
                         tag};
        clip.stage = "DECODE B";
        if (index % 30 == 0)
            clip_snapshot();
        decode_start = sceKernelGetProcessTime();
        code = sceVideodec2Decode(r.decoder, &input, &frame, &output);
        if (++clip.part_index == part_count)
        {
            clip.part_index = 0;
            ++s.submitted;
        }
    }
    else
    {
        if (s.drain_calls >= 12)
            return fail("DRAIN LIMIT", -EIO);
        clip.stage = "DRAIN B";
        ++s.drain_calls;
        decode_start = sceKernelGetProcessTime();
        code = sceVideodec2Flush(r.decoder, &frame, &output);
    }
    const auto decode_us = sceKernelGetProcessTime() - decode_start;
    s.last_decode_us = decode_us;
    s.last_valid = output.valid;
    s.last_accepted = frame.accepted;
    s.last_output_pictures = output.pictures;
    s.decode_max_us = std::max(s.decode_max_us, decode_us);
    if (frame.accepted)
    {
        clip.frame_used[offered] = true;
        if (submitted_slot < clip.slots)
            clip.input_retained[submitted_slot] = true;
    }
    if (code || output.error)
        return fail(clip.stage, code ? code : -EIO);
    if (!output.valid)
    {
        ++s.no_output;
        s.elapsed_us = sceKernelGetProcessTime() - clip.started;
        clip_snapshot();
        s.last_step_us = sceKernelGetProcessTime() - step_start;
        return 2; // Priming/reordering: continue input without flushing the stream.
    }
    unsigned returned = 0;
    while (returned < clip.slots && output.buffer != static_cast<unsigned char *>(r.frame.data) +
                                                returned * clip.frame_stride)
        ++returned;
    const auto required = static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2;
    if (returned == clip.slots || (!clip.frame_used[returned] && returned != offered) ||
        output.pictures != 1 || output.codec != (clip.vp9 ? 0x00245bfdu : 1u) ||
        output.format != 0 || (!clip.vp9 && output.padding) ||
        output.width != clip.width || output.height < clip.height ||
        output.height > (clip.vp9 ? clip.height : 1088) ||
        (output.height & 1u) || output.pitch_bytes < clip.width || output.pitch_bytes > 4096 ||
        (output.pitch_bytes & 1u) || output.bytes > clip.frame_stride || required > output.bytes)
        return fail("OUTPUT B", -EINVAL);
    clip.output_padding = output.padding;
    clip.output_offered = offered;
    clip.output_returned = returned;
    unsigned sum = 0;
    std::uint32_t crc = UINT32_MAX;
    const auto crc_start = sceKernelGetProcessTime();
    for (unsigned y = 0; y < clip.height; y += 31)
        for (unsigned x = 0; x < clip.width; x += 31)
        {
            const unsigned value =
                static_cast<unsigned char *>(output.buffer)[y * output.pitch_bytes + x];
            sum += value;
            crc ^= value;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0);
        }
    crc ^= UINT32_MAX;
    s.last_crc_us = sceKernelGetProcessTime() - crc_start;
    clip.last_crc = crc;
    struct GuardedInfo
    {
        std::uint64_t before = UINT64_C(0xc0dec0dec0dec0de);
        VideoPictureInfo info{};
        std::uint64_t after = UINT64_C(0xc0dec0dec0dec0de);
    } picture;
    picture.info.size = sizeof(picture.info);
    picture.info.pts = picture.info.dts = picture.info.attached = UINT64_MAX;
    if (clip.vp9)
    {
        Vp9PendingOutput pending[6]{};
        for (unsigned i = 0; i < clip.slots; ++i)
            pending[i] = {clip.input_tag[i], clip.input_displayable[i], clip.input_retained[i]};
        const auto choice = select_vp9_output({pending, clip.slots}, output.padding);
        if (choice.error != Vp9OutputError::none)
            return fail("VP9 OUTPUT FLAG POLICY", -EINVAL);
        for (unsigned i = 0; i < clip.slots; ++i)
            if (choice.retire_mask & (1u << i))
            {
                clip.input_tag[i] = 0;
                ++s.hidden_no_output;
            }
        const unsigned oldest = choice.input;
        // Selection is pixel-independent; the CRC below only verifies its result.
        const unsigned index = clip.input_packet[oldest];
        picture.info.valid = 1;
        picture.info.attached = clip.input_tag[oldest];
        picture.info.pts = static_cast<std::uint64_t>(clip.pts_index[index]) * 1000000 / s.fps;
        picture.info.dts = static_cast<std::uint64_t>(index) * 1000000 / s.fps;
    }
    else
        code = sceVideodec2GetPictureInfo(&output, &picture.info, nullptr);
    s.last_pts = picture.info.pts;
    s.last_dts = picture.info.dts;
    s.last_attached = picture.info.attached;
    if (code || picture.before != UINT64_C(0xc0dec0dec0dec0de) ||
        picture.after != UINT64_C(0xc0dec0dec0dec0de) || picture.info.valid != 1 ||
        picture.info.size != sizeof(picture.info) || !picture.info.attached ||
        (!clip.vp9 && picture.info.attached > s.submitted))
        return fail("PICTURE INFO", code ? code : -EINVAL);
    ++s.metadata;
    unsigned input_slot = 0;
    while (input_slot < clip.slots && clip.input_tag[input_slot] != picture.info.attached)
        ++input_slot;
    if (input_slot == clip.slots)
        return fail("PICTURE INPUT SLOT", -EINVAL);
    const unsigned packet = clip.vp9 ? clip.input_packet[input_slot]
                                    : static_cast<unsigned>(picture.info.attached - 1);
    if (
        picture.info.pts != static_cast<std::uint64_t>(clip.pts_index[packet]) * 1000000 / s.fps ||
        picture.info.dts != static_cast<std::uint64_t>(packet) * 1000000 / s.fps)
        return fail("PICTURE TAG", -EINVAL);
    if (clip.vp9 && !clip.input_displayable[input_slot])
    {
        if (!output.padding)
            return fail("HIDDEN SURFACE FLAG", -EINVAL);
        ++clip.hidden_materialized;
        clip.frame_used[returned] = false;
        clip.input_tag[input_slot] = 0;
        clip.stage = "PIPELINE HIDDEN SUPPRESSED";
        s.last_step_us = sceKernelGetProcessTime() - step_start;
        return 2;
    }
    if (clip.pts_index[packet] != s.decoded)
    {
        ++s.order_errors;
        return fail("PRESENTATION ORDER", -EINVAL);
    }
    clip.expected_crc = clip.crc[packet];
    if (crc != clip.crc[packet])
        return fail("PICTURE CRC", -EILSEQ);
    ++s.crc_matches;
    if (!s.decoded)
        clip.first_sum = sum;
    else if (sum != clip.last_sum)
        ++s.changed_frames;
    clip.last_sum = sum;
    ++s.decoded;
    const auto convert_start = sceKernelGetProcessTime();
    code = consumer(output, context);
    const auto convert_us = sceKernelGetProcessTime() - convert_start;
    s.last_consumer_us = convert_us;
    s.convert_max_us = std::max(s.convert_max_us, convert_us);
    if (code)
        return fail("CONSUMER B", code);
    clip.frame_used[returned] = false;
    clip.input_tag[input_slot] = 0;
    s.elapsed_us = sceKernelGetProcessTime() - clip.started;
    clip.stage = "B FRAME COPIED";
    if (s.decoded % 30 == 0 || s.decoded == s.frames)
        clip_snapshot();
    s.last_step_us = sceKernelGetProcessTime() - step_start;
    clip.last_work_us = s.last_step_us;
    return 0;
}
#endif
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
    if (s.bframes || clip.pipeline)
        return hardware_bclip_step(consumer, context);
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
#ifdef TUBE5_HW_TIMING
    const auto step_start = sceKernelGetProcessTime();
    s.last_log_us = 0;
#endif
    if (::lseek(clip.descriptor, clip.offsets[index], SEEK_SET) < 0 ||
        read_exact(clip.descriptor, r.input.data, clip.sizes[index]))
        return fail("READ", -EIO);
#ifdef TUBE5_HW_TIMING
    s.last_read_us = sceKernelGetProcessTime() - step_start;
#endif
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
    int code = 0;
    if (clip.vp9)
    {
        Vp9Packet packet{};
        if (!split_vp9_packet(static_cast<const std::uint8_t *>(r.input.data), clip.sizes[index],
                              packet))
            return fail("VP9 PACKET INDEX", -EINVAL);
        unsigned visible = 0;
        for (unsigned i = 0; i < packet.count; ++i)
            visible += packet.parts[i].displayable;
        if (visible != 1 || !packet.parts[packet.count - 1].displayable ||
            (!clip.compound_vp9 && packet.count != 1))
            return fail("VP9 FIXTURE PACKET LAYOUT", -EINVAL);
        s.superframes += packet.count > 1;
        for (unsigned i = 0; i < packet.count; ++i)
        {
            const auto &part = packet.parts[i];
            input.au = const_cast<std::uint8_t *>(part.data);
            input.bytes = part.bytes;
            frame = {sizeof(VideoFrame), r.frame.data, r.frame.bytes, 0, 0};
            output = {};
            output.size = sizeof(output);
            code = sceVideodec2Decode(r.decoder, &input, &frame, &output);
            ++s.coded_frames;
            s.last_valid = output.valid;
            s.last_accepted = frame.accepted;
            s.last_output_pictures = output.pictures;
            s.existing_frames += part.existing;
            if (code || output.error)
                return fail(part.displayable ? "VP9 VISIBLE DECODE" : "VP9 HIDDEN DECODE",
                            code ? code : -EIO);
            if (!part.displayable && !output.valid)
            {
                // A hidden reference need not produce a presentation image. Never
                // reuse a surface retained by the driver in this single-slot test.
                if (frame.accepted)
                    return fail("VP9 HIDDEN SURFACE RETAINED", -EINVAL);
                ++s.hidden_frames;
                ++s.hidden_no_output;
                continue;
            }
            const auto required =
                static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2;
            if (!output.valid || output.buffer != r.frame.data || output.pictures != 1 ||
                output.codec != 0x00245bfdu || output.format != 0 || output.width != clip.width ||
                output.height != clip.height || output.pitch_bytes < clip.width ||
                output.pitch_bytes > 4096 || (output.pitch_bytes & 1u) ||
                output.bytes > r.frame.bytes || required > output.bytes)
                return fail("VP9 SYNC OUTPUT LAYOUT", -EINVAL);
            if (!part.displayable)
                ++s.hidden_frames; // Decode references, but never upload or advance the
                                   // presentation clock.
        }
    }
    else
        code = sceVideodec2Decode(r.decoder, &input, &frame, &output);
    ++s.submitted;
    if (code || output.error)
        return fail("DECODE", code ? code : -EIO);
    if (!output.valid)
    {
        if (clip.vp9)
            return fail("VP9 VISIBLE FRAME REQUIRED", -EINVAL);
        output = {};
        output.size = sizeof(output);
        code = sceVideodec2Flush(r.decoder, &frame, &output);
    }
    const auto decode_us = sceKernelGetProcessTime() - decode_start;
    s.decode_max_us = std::max(s.decode_max_us, decode_us);
#ifdef TUBE5_HW_TIMING
    s.last_decode_us = decode_us;
#endif
    const auto required = static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2;
    if (code || output.error || !output.valid || output.pictures != 1 ||
        output.codec != (clip.vp9 ? 0x00245bfdu : 1u) || output.format != 0 ||
        output.buffer != r.frame.data || output.width != clip.width ||
        output.height < clip.height || output.height > (clip.vp9 ? clip.height : 1088u) ||
        (output.height & 1u) || output.pitch_bytes < clip.width || output.pitch_bytes > 4096 ||
        (output.pitch_bytes & 1u) || output.bytes > r.frame.bytes || required > output.bytes)
        return fail("OUTPUT", code ? code : -EINVAL);
#ifdef TUBE5_HW_TIMING
    const auto crc_start = sceKernelGetProcessTime();
#endif
    unsigned sum = 0;
    std::uint32_t crc = UINT32_MAX;
    for (unsigned y = 0; y < clip.height; y += 31)
        for (unsigned x = 0; x < clip.width; x += 31)
        {
            const unsigned value =
                static_cast<unsigned char *>(output.buffer)[y * output.pitch_bytes + x];
            sum += value;
            crc ^= value;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0);
        }
    if (clip.vp9)
    {
        clip.last_crc = crc ^ UINT32_MAX;
        clip.expected_crc = clip.crc[index];
        if (clip.last_crc != clip.expected_crc)
            return fail("VP9 PICTURE CRC", -EILSEQ);
        ++s.crc_matches;
    }
#ifdef TUBE5_HW_TIMING
    s.last_crc_us = sceKernelGetProcessTime() - crc_start;
#endif
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
#ifdef TUBE5_HW_TIMING
    s.last_consumer_us = convert_us;
#endif
    if (code)
        return fail("CONSUMER", code);
    s.elapsed_us = sceKernelGetProcessTime() - clip.started;
    clip.stage = "FRAME COPIED";
    if (s.submitted % 30 == 0 || s.submitted == s.frames)
        clip_snapshot();
#ifdef TUBE5_HW_TIMING
    s.last_step_us = sceKernelGetProcessTime() - step_start;
    clip.last_work_us = s.last_step_us;
#endif
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
#ifdef TUBE5_HW_FRAME
static int stream_failure(const char *stage, int code)
{
    stream_status.stage = stage;
    stream_status.error = code;
    return code;
}
static int stream_output(unsigned offered, const VideoFrame &frame, const VideoOutput &output)
{
    stream_status.last_output = output;
    stream_status.last_offered = offered;
    stream_status.last_frame_accepted = frame.accepted;
    stream_status.frame_capacity = clip.frame_stride;
    if (frame.accepted)
    {
        ++stream_status.accepted;
        stream.frame_used[offered] = true;
        if (!stream.draining)
            stream.input[stream.sync_input].retained = true;
    }
    if (output.error)
        return stream_failure("OUTPUT ERROR", -EIO);
    if (!output.valid)
    {
        ++stream_status.no_output;
        if (stream.vp9 && !stream.pipeline)
        {
            if (stream.displayable || frame.accepted)
                return stream_failure("VP9 SYNC NO OUTPUT", -EINVAL);
            stream.input[stream.sync_input] = {};
            ++stream_status.hidden;
        }
        return 0;
    }
    unsigned returned = 0;
    while (returned < stream.slots && output.buffer != static_cast<unsigned char *>(resources.frame.data) +
                                                returned * clip.frame_stride)
        ++returned;
    stream_status.last_returned = returned;
    const auto bytes = static_cast<std::uint64_t>(output.pitch_bytes) * output.height * 3 / 2;
    if (returned == stream.slots || (!stream.frame_used[returned] && returned != offered) ||
        output.codec != (stream.vp9 ? 0x00245bfdu : 1u) || output.pictures != 1 ||
        // VP9 hidden pictures legitimately set byte 11 to 1 on FW 13.20.
        // It is not a layout/error field; keep all actual surface bounds checks.
        output.format != 0 || (!stream.vp9 && output.padding) || output.width != stream.width ||
        output.height < stream.height || output.height > (stream.vp9 ? stream.height : 1088u) ||
        (output.height & 1u) || output.pitch_bytes < output.width || output.pitch_bytes > 4096 ||
        (output.pitch_bytes & 1u) || output.bytes > clip.frame_stride || bytes > output.bytes)
        return stream_failure("OUTPUT LAYOUT", -EINVAL);
    struct Guarded
    {
        std::uint64_t before = UINT64_C(0xc0dec0dec0dec0de);
        VideoPictureInfo picture{};
        std::uint64_t after = UINT64_C(0xc0dec0dec0dec0de);
    } info;
    info.picture.size = sizeof(info.picture);
    info.picture.pts = info.picture.dts = info.picture.attached = UINT64_MAX;
    int code = 0;
    if (stream.vp9)
    {
        unsigned oldest = stream.sync_input;
        if (stream.pipeline)
        {
            Vp9PendingOutput pending[6]{};
            for (unsigned i = 0; i < stream.input_slots; ++i)
                pending[i] = {stream.input[i].tag, stream.input[i].displayable,
                              stream.input[i].retained};
            const auto choice = select_vp9_output({pending, stream.input_slots}, output.padding);
            stream_status.policy_error = static_cast<unsigned>(choice.error);
            if (choice.error != Vp9OutputError::none)
                return stream_failure("VP9 OUTPUT FLAG POLICY", -EINVAL);
            oldest = choice.input;
            for (unsigned i = 0; i < stream.input_slots; ++i)
                if (choice.retire_mask & (1u << i))
                {
                    stream.input[i] = {};
                    ++stream_status.hidden;
                    ++stream_status.hidden_omitted;
                }
        }
        // Visible outputs keep FIFO; hidden surfaces can overtake visible ones.
        // Metadata selection never inspects pixels or fixture CRCs.
        const auto &submitted = stream.input[oldest];
        info.picture.valid = 1;
        info.picture.pts = submitted.pts;
        info.picture.dts = submitted.dts;
        info.picture.attached = submitted.tag;
    }
    else
        code = sceVideodec2GetPictureInfo(&output, &info.picture, nullptr);
    if (code || info.picture.valid != 1 || info.picture.size != sizeof(info.picture) ||
        info.before != UINT64_C(0xc0dec0dec0dec0de) || info.after != UINT64_C(0xc0dec0dec0dec0de))
        return stream_failure("PICTURE INFO", code ? code : -EINVAL);
    unsigned input = 0;
    while (input < stream.input_slots && stream.input[input].tag != info.picture.attached)
        ++input;
    if (input == stream.input_slots || !info.picture.attached || stream.input[input].pts != info.picture.pts ||
        stream.input[input].dts != info.picture.dts)
        return stream_failure("PICTURE TAG", -EINVAL);
    const bool displayable = stream.input[input].displayable;
    stream.input[input] = {};
    if (stream.vp9 && !displayable)
    {
        stream.frame_used[returned] = false;
        ++stream_status.hidden;
        if (stream.pipeline)
            ++stream_status.hidden_materialized;
        return 0;
    }
    stream.frame_used[returned] = true;
    stream.returned = returned;
    stream.output = output;
    stream.picture = info.picture;
    stream.ready = true;
    return 0;
}
#endif
const HardwareStreamStatus &hardware_stream_status() noexcept
{
    return stream_status;
}
int hardware_stream_close() noexcept
{
#ifndef TUBE5_HW_FRAME
    return -1;
#else
    if (!stream_status.active)
        return stream_status.cleanup_error;
    stream_status.cleanup_error = cleanup();
    resources.quarantined = stream_status.cleanup_error != 0;
    stream_status.active = false;
    if (!stream_status.error)
        stream_status.stage = "CLOSED";
    return stream_status.cleanup_error;
#endif
}
int hardware_stream_reset() noexcept
{
#ifndef TUBE5_HW_FRAME
    return -1;
#else
    if (!stream_status.active || stream_status.error)
        return stream_status.error ? stream_status.error : -EINVAL;
    const int code = sceVideodec2Reset(resources.decoder);
    if (code)
        return stream_failure("RESET", code);
    const auto next_tag = stream.next_tag;
    const auto vp9 = stream.vp9;
    const auto pipeline = stream.pipeline;
    const auto slots = stream.slots;
    const auto input_slots = stream.input_slots;
    const auto width = stream.width, height = stream.height;
    stream = {};
    stream.vp9 = vp9;
    stream.pipeline = pipeline;
    stream.slots = slots;
    stream.input_slots = input_slots;
    stream.width = width;
    stream.height = height;
    stream.next_tag = next_tag;
    ++stream_status.resets;
    stream_status.stage = "READY";
    return 0;
#endif
}
int hardware_stream_begin(HardwareFrameMode mode, bool vp9_pipeline) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)mode;
    (void)vp9_pipeline;
    return -1;
#else
    if (stream_status.active || clip_status.active || resources.quarantined || resources.decoder ||
        resources.queue)
        return -EBUSY;
    stream = {};
    stream.vp9 = mode != HardwareFrameMode::avc1080;
    stream.pipeline = stream.vp9 && vp9_pipeline;
    // The async diagnostic is restricted to the validated 4K visible fixture.
    if (vp9_pipeline && mode != HardwareFrameMode::vp9_2160)
        return -EINVAL;
    stream.slots = stream.pipeline ? 4 : stream.vp9 ? 1 : 6;
    stream.input_slots = stream.pipeline ? 6 : stream.slots;
    stream.width = mode == HardwareFrameMode::vp9_2160 ? 3840 : stream.vp9 ? 2560 : 1920;
    stream.height = mode == HardwareFrameMode::vp9_2160 ? 2160 : stream.vp9 ? 1440 : 1080;
    stream_status = {};
    stream_status.active = true;
    stream_status.depth = stream.pipeline || !stream.vp9 ? 3 : 1;
    stream_status.slots = stream.slots;
    stream_status.input_slots = stream.input_slots;
    const auto record = [](const char *stage, int code)
    {
        stream_status.stage = stage;
        stream_status.error = code;
    };
    VideoDecoderConfig requested{};
    requested.codec = 0x00245bfd;
    requested.profile = 0;
    requested.level = stream.width == 3840 ? 51 : 50;
    requested.width = stream.width;
    requested.height = stream.height;
    int code = prepare_decoder(record, stream_status.depth, stream.slots,
                               stream.vp9 ? &requested : nullptr, stream.input_slots);
    if (!code)
        code = hardware_stream_reset();
    if (code)
        hardware_stream_close();
    return code;
#endif
}
int hardware_stream_send(const std::uint8_t *data, std::size_t bytes, std::uint64_t pts,
                         std::uint64_t dts, bool displayable) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)data;
    (void)bytes;
    (void)pts;
    (void)dts;
    (void)displayable;
    return -1;
#else
    if (!stream_status.active || stream_status.error)
        return stream_status.error ? stream_status.error : -EINVAL;
    if (stream.ready || stream.borrowed)
        return 2;
    if (!data)
    {
        stream.draining = true;
        return 0;
    }
    if (stream.draining)
        return 1;
    if (!bytes || bytes >= mib)
        return stream_failure("AU SIZE", -EINVAL);
    unsigned input = 0, offered = 0;
    while (input < stream.input_slots && stream.input[input].tag)
        ++input;
    while (offered < stream.slots && stream.frame_used[offered])
        ++offered;
    if (input == stream.input_slots || offered == stream.slots)
        return stream_failure(input == stream.input_slots ? "INPUT POOL FULL" : "FRAME POOL FULL",
                              -ENOBUFS);
    auto *packet = static_cast<unsigned char *>(resources.input.data) + input * mib;
    std::memcpy(packet, data, bytes);
    stream_status.last_au_bytes = bytes;
    stream_status.last_input_pts = pts;
    stream_status.last_input_dts = dts;
    stream_status.last_nal_mask = 0;
    for (std::size_t i = 0; i + 3 < bytes; ++i)
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            stream_status.last_nal_mask |= 1u << (data[i + 3] & 31u);
    const auto tag = stream.next_tag++;
    stream.input[input] = {tag, pts, dts, displayable, false};
    unsigned pending_count = 0;
    for (const auto &pending : stream.input)
        pending_count += pending.tag != 0;
    stream_status.pending_peak = std::max(stream_status.pending_peak, pending_count);
    stream.sync_input = input;
    stream.displayable = displayable;
    VideoInput au{sizeof(VideoInput), packet, bytes, pts, dts, tag};
    VideoFrame frame{sizeof(VideoFrame),
                     static_cast<unsigned char *>(resources.frame.data) +
                         offered * clip.frame_stride,
                     clip.frame_stride, 0, 0};
    VideoOutput output{};
    output.size = sizeof(output);
    const auto decode_start = sceKernelGetProcessTime();
    const int code = sceVideodec2Decode(resources.decoder, &au, &frame, &output);
    const auto decode_us = sceKernelGetProcessTime() - decode_start;
    stream_status.decode_total_us += decode_us;
    stream_status.decode_max_us = std::max(stream_status.decode_max_us, decode_us);
    ++stream_status.decode_calls;
    ++stream_status.submitted;
    if (code)
        return stream_failure("DECODE", code);
    return stream_output(offered, frame, output);
#endif
}
int hardware_stream_receive(VideoOutput &output, VideoPictureInfo &picture) noexcept
{
#ifndef TUBE5_HW_FRAME
    (void)output;
    (void)picture;
    return -1;
#else
    if (!stream_status.active || stream_status.error)
        return stream_status.error ? stream_status.error : -EINVAL;
    if (stream.borrowed)
        return 2;
    while (!stream.ready && stream.draining)
    {
        bool pending = false;
        for (const auto &input : stream.input)
            pending |= input.tag != 0;
        if (!pending)
            return 1;
        unsigned offered = 0;
        while (offered < stream.slots && stream.frame_used[offered])
            ++offered;
        if (offered == stream.slots)
            return stream_failure("DRAIN POOL", -ENOBUFS);
        ++stream_status.drain_calls;
        if (++stream.drain_attempts > 12)
            return stream_failure("DRAIN LIMIT", -EIO);
        VideoFrame frame{sizeof(VideoFrame),
                         static_cast<unsigned char *>(resources.frame.data) +
                             offered * clip.frame_stride,
                         clip.frame_stride, 0, 0};
        VideoOutput decoded{};
        decoded.size = sizeof(decoded);
        int code = sceVideodec2Flush(resources.decoder, &frame, &decoded);
        if (code)
            return stream_failure("FLUSH", code);
        code = stream_output(offered, frame, decoded);
        if (code)
            return code;
        if (!stream.ready && !stream.pipeline)
            return stream_failure("INCOMPLETE DRAIN", -EIO);
    }
    if (!stream.ready)
        return 2;
    output = stream.output;
    picture = stream.picture;
    stream.ready = false;
    stream.borrowed = true;
    ++stream_status.received;
    return 0;
#endif
}
void hardware_stream_release() noexcept
{
#ifdef TUBE5_HW_FRAME
    if (stream.borrowed)
    {
        stream.frame_used[stream.returned] = false;
        stream.borrowed = false;
    }
#endif
}
} // namespace tube5
