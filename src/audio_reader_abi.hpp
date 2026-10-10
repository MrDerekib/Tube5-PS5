// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <kitchensink2/kitformat.h>
extern "C"
{
#include <libavformat/avformat.h>
}
namespace tube5::audio_detail
{
// Private ABI of pinned SDL_kitchensink c1bac528 (PacBrew 2.0a2).
// Access via memcpy, not type-punning. Host tests compare the decoder prefix
// with upstream headers. Re-audit these layouts before changing that library.
struct DecoderPrefix
{
    void *timer;
    AVRational aspect;
    void *codec;
    AVStream *stream;
    int hw_format;
    int hw_type;
    void *userdata;
};
struct AudioPrefix
{
    void *resampler;
    AVFrame *input;
    AVFrame *output_frame;
    AVFrame *current;
    void *fifo;
    std::int64_t fifo_pts;
    void *buffer;
    Kit_AudioOutputFormat output;
};
static_assert(offsetof(DecoderPrefix, userdata) == 0x28);
static_assert(offsetof(AudioPrefix, current) == 0x18);
static_assert(offsetof(AudioPrefix, buffer) == 0x30);
static_assert(offsetof(AudioPrefix, output) == 0x38);
} // namespace tube5::audio_detail
