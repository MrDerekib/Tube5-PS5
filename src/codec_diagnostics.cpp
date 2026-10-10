// SPDX-License-Identifier: GPL-3.0-or-later
#include "codec_diagnostics.hpp"

namespace
{
char last_error[128]{};
}
namespace tube5
{
void reset_codec_diagnostic() noexcept
{
    last_error[0] = '\0';
}
const char *codec_diagnostic() noexcept
{
    return last_error;
}
} // namespace tube5

#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
#include "boot_diagnostics.hpp"
#include <cstdio>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
    int __real_avcodec_open2(AVCodecContext *, const AVCodec *, AVDictionary **);
}

// SDL_kitchensink discards avcodec_open2's negative return code. Keep that
// detail without changing the codec, its options or FFmpeg's return value.
extern "C" int __wrap_avcodec_open2(AVCodecContext *context, const AVCodec *codec,
                                    AVDictionary **options)
{
    const int requested_threads = context ? context->thread_count : 0;
    const int result = __real_avcodec_open2(context, codec, options);
    if (result < 0)
    {
        char detail[AV_ERROR_MAX_STRING_SIZE]{};
        if (av_strerror(result, detail, sizeof(detail)) != 0)
            std::snprintf(detail, sizeof(detail), "unknown FFmpeg error");
        std::snprintf(last_error, sizeof(last_error), "FFmpeg %d: %s", result, detail);
        tube5_boot_checkpoint("Tube5 CODEC: apertura fallida");
        tube5_boot_checkpoint(codec && codec->name ? codec->name : "unknown codec");
        tube5_boot_checkpoint(last_error);
        tube5_boot_result("Tube5 CODEC: hilos solicitados",
                          static_cast<std::uint32_t>(requested_threads));
    }
    return result;
}
#endif
