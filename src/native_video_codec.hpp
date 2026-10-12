// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
namespace tube5
{
#ifdef TUBE5_HW_PLAYER
void native_codec_allow_open(bool allow) noexcept;
void native_codec_adopt(AVCodecContext *context) noexcept;
bool native_codec_owns(const AVCodecContext *context) noexcept;
int native_codec_send(AVCodecContext *context, const AVPacket *packet) noexcept;
int native_codec_receive(AVCodecContext *context, AVFrame *frame) noexcept;
int native_codec_error() noexcept;
bool native_codec_active() noexcept;
unsigned native_codec_reset_generation() noexcept;
unsigned native_codec_audio_reset_generation() noexcept;
#else
inline void native_codec_allow_open(bool) noexcept
{
}
inline void native_codec_adopt(AVCodecContext *) noexcept
{
}
inline bool native_codec_owns(const AVCodecContext *) noexcept
{
    return false;
}
inline int native_codec_error() noexcept
{
    return 0;
}
inline bool native_codec_active() noexcept
{
    return false;
}
inline unsigned native_codec_reset_generation() noexcept
{
    return 0;
}
inline unsigned native_codec_audio_reset_generation() noexcept
{
    return 0;
}
#endif
} // namespace tube5
