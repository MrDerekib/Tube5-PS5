// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_player.hpp"
#include "codec_diagnostics.hpp"
#include <algorithm>
#include <cstdio>

namespace tube5
{
bool MediaPlayer::fail(const char *stage, const char *detail) noexcept
{
    std::snprintf(error_, sizeof(error_), "%s: %s", stage, detail ? detail : "unknown error");
    close();
    return false;
}

bool MediaPlayer::open(SDL_Renderer *renderer, const char *path) noexcept
{
    close();
    error_[0] = '\0';
    source_ = Kit_CreateSourceFromUrl(path);
    if (!source_)
        return fail("SOURCE", Kit_GetError());
    const int video = Kit_GetBestSourceStream(source_, KIT_STREAMTYPE_VIDEO);
    const int audio = Kit_GetBestSourceStream(source_, KIT_STREAMTYPE_AUDIO);
    if (video < 0 || audio < 0)
        return fail("STREAMS", "test clip must contain video and audio");
    Kit_VideoFormatRequest video_request{};
    Kit_ResetVideoFormatRequest(&video_request);
    video_request.format = SDL_PIXELFORMAT_RGBA32;
    video_request.hw_device_types = KIT_HWDEVICE_TYPE_NONE;
    Kit_AudioFormatRequest audio_request{};
    Kit_ResetAudioFormatRequest(&audio_request);
    audio_request.format = AUDIO_S16SYS;
    audio_request.is_signed = 1;
    audio_request.bytes = 2;
    audio_request.sample_rate = 48000;
    audio_request.channels = 2;
    // Establish a conservative native baseline before trying parallel codec
    // workers. This does NOT disable Kitchensink's demux/audio/video threads.
    Kit_SetHint(KIT_HINT_THREAD_COUNT, 1);
    reset_codec_diagnostic();
    player_ =
        Kit_CreatePlayer(source_, video, audio, -1, &video_request, &audio_request, 1920, 1080);
    if (!player_)
        return fail("DECODER", codec_diagnostic()[0] ? codec_diagnostic() : Kit_GetError());
    Kit_PlayerInfo info{};
    Kit_GetPlayerInfo(player_, &info);
    width_ = info.video_format.width;
    height_ = info.video_format.height;
    if (width_ <= 0 || height_ <= 0 || width_ > 1920 || height_ > 1080)
        return fail("VIDEO", "unsupported test frame size");
    texture_ = SDL_CreateTexture(renderer, info.video_format.format, SDL_TEXTUREACCESS_STATIC,
                                 width_, height_);
    if (!texture_)
        return fail("TEXTURE", SDL_GetError());
    SDL_AudioSpec desired{};
    desired.freq = info.audio_format.sample_rate;
    desired.format = static_cast<SDL_AudioFormat>(info.audio_format.format);
    desired.channels = static_cast<Uint8>(info.audio_format.channels);
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = this;
    audio_ = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!audio_)
        return fail("AUDIO", SDL_GetError());
    Kit_PlayerPlay(player_);
    SDL_PauseAudioDevice(audio_, 0);
    return true;
}

void MediaPlayer::audio_callback(void *userdata, Uint8 *stream, int length) noexcept
{
    SDL_memset(stream, 0, static_cast<std::size_t>(length));
    auto *self = static_cast<MediaPlayer *>(userdata);
    if (self->player_)
        (void)Kit_GetPlayerAudioData(self->player_, 0, stream, static_cast<std::size_t>(length));
}

void MediaPlayer::close() noexcept
{
    // Stop and join the callback before releasing the decoder it reads.
    if (audio_)
        SDL_CloseAudioDevice(audio_);
    audio_ = 0;
    if (player_)
        Kit_ClosePlayer(player_);
    player_ = nullptr;
    if (texture_)
        SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    if (source_)
        Kit_CloseSource(source_);
    source_ = nullptr;
    paused_ = false;
    paused_position_ = 0.0;
    pending_seek_ = -1.0;
    seek_dispatched_at_ = 0;
}

void MediaPlayer::toggle_pause() noexcept
{
    if (!player_)
        return;
    const bool was_paused = paused();
    SDL_PauseAudioDevice(audio_, 1);
    if (was_paused)
        Kit_PlayerPlay(player_);
    else
    {
        paused_position_ = Kit_GetPlayerPosition(player_);
        Kit_PlayerPause(player_);
    }
    paused_ = !was_paused;
    if (was_paused)
        SDL_PauseAudioDevice(audio_, 0);
}

void MediaPlayer::seek(double delta) noexcept
{
    if (!player_)
        return;
    const bool was_paused = paused();
    const double base = pending_seek_ >= 0.0 ? pending_seek_ : position();
    const double target = std::clamp(base + delta, 0.0, std::max(0.0, duration() - 0.1));
    pending_seek_ = target;
    seek_requested_at_ = SDL_GetTicks();
    if (was_paused)
        paused_position_ = target;
}

void MediaPlayer::draw(SDL_Renderer *renderer, const SDL_Rect &destination) noexcept
{
    if (!player_ || !texture_)
        return;
    // a2 silently drops a seek if the demuxer already has one pending. Coalesce
    // quick button presses, and defer paused seeks until buffers can drain.
    const Uint32 now = SDL_GetTicks();
    if (!paused_ && pending_seek_ >= 0.0 && now - seek_requested_at_ >= 100 &&
        (seek_dispatched_at_ == 0 || now - seek_dispatched_at_ >= 500))
    {
        SDL_PauseAudioDevice(audio_, 1);
        if (Kit_PlayerSeek(player_, pending_seek_) != 0)
            std::snprintf(error_, sizeof(error_), "SEEK: %s", Kit_GetError());
        pending_seek_ = -1.0;
        seek_dispatched_at_ = now;
        SDL_PauseAudioDevice(audio_, 0);
    }
    SDL_Rect source{0, 0, width_, height_};
    (void)Kit_GetPlayerVideoSDLTexture(player_, texture_, &source);
    SDL_Rect target = destination;
    // Letterbox instead of stretching non-16:9 content.
    if (width_ * destination.h > height_ * destination.w)
    {
        target.h = destination.w * height_ / width_;
        target.y += (destination.h - target.h) / 2;
    }
    else
    {
        target.w = destination.h * width_ / height_;
        target.x += (destination.w - target.w) / 2;
    }
    (void)SDL_RenderCopy(renderer, texture_, &source, &target);
}

double MediaPlayer::position() const noexcept
{
    // Kitchensink a2 keeps its clock advancing while paused. Present our frozen
    // timestamp until it resynchronizes to decoded PTS after resume.
    return player_ ? (paused_ ? paused_position_ : Kit_GetPlayerPosition(player_)) : 0.0;
}
double MediaPlayer::duration() const noexcept
{
    return player_ ? Kit_GetPlayerDuration(player_) : 0.0;
}
bool MediaPlayer::paused() const noexcept
{
    return player_ && paused_;
}
bool MediaPlayer::finished() const noexcept
{
    return player_ && !paused_ && Kit_GetPlayerState(player_) == KIT_STOPPED;
}
} // namespace tube5
