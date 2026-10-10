// SPDX-License-Identifier: GPL-3.0-or-later
#include "codec_diagnostics.hpp"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
extern "C"
{
#include <libavcodec/avcodec.h>
    int __wrap_avcodec_open2(AVCodecContext *, const AVCodec *, AVDictionary **);
}
namespace
{
AVCodecContext context{};
AVCodec codec{};
AVDictionary *options = nullptr;
int open_result = 0;
std::vector<std::string> messages;
std::vector<std::uint32_t> values;
} // namespace
extern "C" int __real_avcodec_open2(AVCodecContext *received_context, const AVCodec *received_codec,
                                    AVDictionary **received_options)
{
    assert(received_context == &context && received_codec == &codec &&
           received_options == &options);
    assert(context.thread_count == 1);
    return open_result;
}
extern "C" void tube5_boot_checkpoint(const char *message) noexcept
{
    messages.emplace_back(message);
}
void tube5_boot_result(const char *, std::uint32_t value) noexcept
{
    values.push_back(value);
}
int main()
{
    context.thread_count = 1;
    codec.name = "h264";
    tube5::reset_codec_diagnostic();
    assert(__wrap_avcodec_open2(&context, &codec, &options) == 0);
    assert(tube5::codec_diagnostic()[0] == '\0' && messages.empty());
    open_result = AVERROR(ENOMEM);
    assert(__wrap_avcodec_open2(&context, &codec, &options) == open_result);
    assert(std::strstr(tube5::codec_diagnostic(), "FFmpeg -12:") != nullptr);
    assert(messages.size() == 3 && messages[1] == "h264");
    assert(messages[2] == tube5::codec_diagnostic());
    assert((values == std::vector<std::uint32_t>{1}));
    tube5::reset_codec_diagnostic();
    assert(tube5::codec_diagnostic()[0] == '\0');
    open_result = 1; // Any nonnegative FFmpeg result is passed through unchanged.
    assert(__wrap_avcodec_open2(&context, &codec, &options) == 1);
    assert(tube5::codec_diagnostic()[0] == '\0');
}
