// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_player.hpp"
#include <cmath>
#include "audio_reader.hpp"
#include "codec_diagnostics.hpp"
#include "thread_diagnostics.hpp"
#include "pipeline_diagnostics.hpp"
#include "memory_diagnostics.hpp"
#include "network_source.hpp"
#include "shared_playback_clock.hpp"
#include "failure_log.hpp"
#include "native_parser_buffers.hpp"
#include "native_refstruct_buffers.hpp"
#include "native_surface_buffers.hpp"
#include "native_media_buffers.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
extern "C"
{
#include <libavutil/error.h>
}

namespace tube5
{
bool MediaPlayer::fail(const char *stage, const char *detail) noexcept
{
    if (memory_out_of_memory())
        memory_blocked_ = true;
    std::snprintf(error_, sizeof(error_), "%s: %s", stage, detail ? detail : "unknown error");
    const double failure_position = position();
    const unsigned failure_frames = frames_uploaded_;
    close();
    save_failure_report(stage, failure_position, failure_frames);
    return false;
}
void MediaPlayer::save_failure_report(const char *stage, double position, unsigned frames) noexcept
{
#ifdef TUBE5_DIAGNOSTICS
    char report[4096]{}, line[192]{};
    std::size_t used = 0;
    const auto append = [&](const char *text)
    {
        const int length = std::snprintf(report + used, sizeof(report) - used, "%s\n", text);
        if (length > 0)
            used += std::min<std::size_t>(length, sizeof(report) - used - 1);
    };
    append("TUBE5 FAILURE SNAPSHOT V1 - NUMERIC DIAGNOSTICS ONLY");
    std::snprintf(line, sizeof(line), "BUILD %s", failure_log_build_label());
    append(line);
    std::snprintf(line, sizeof(line), "POSITION MS %.0f FRAMES %u", position * 1000.0, frames);
    append(line);
    std::snprintf(line, sizeof(line), "STAGE %s VIDEO %dx%d", stage, width_, height_);
    append(line);
    for (unsigned row = 0; row < 6; ++row)
    {
        youtube_diagnostic(row, line, sizeof(line));
        append(line);
    }
    memory_diagnostic_status(line, sizeof(line));
    append(line);
    memory_failure_stage_status(line, sizeof(line));
    append(line);
    flexible_memory_status(line, sizeof(line));
    append(line);
    for (unsigned row = 0; row < 2; ++row)
    {
        memory_allocation_origin_status(row, line, sizeof(line));
        append(line);
    }
    for (unsigned row = 0; row < 3; ++row)
    {
        pipeline_diagnostic_status(row, line, sizeof(line));
        append(line);
    }
#ifdef TUBE5_PARSER_BUFFER_WRAP
    native_parser_buffer_status(line, sizeof(line));
    append(line);
#endif
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
    native_refstruct_buffer_status(line, sizeof(line));
    append(line);
#endif
#ifdef TUBE5_SURFACE_BUFFER_WRAP
    native_surface_buffer_status(line, sizeof(line));
    append(line);
#endif
#ifdef TUBE5_MEDIA_BUFFER_WRAP
    native_media_buffer_status(line, sizeof(line));
    append(line);
#endif
#ifdef TUBE5_HOST
    const char *path = ".local/tube5-debug.log";
#else
    const char *path = "/download0/tube5-debug.log";
#endif
    const int result = write_failure_log(path, report, used);
    std::snprintf(failure_log_status_, sizeof(failure_log_status_),
                  result ? "LOG NO GUARDADO ERR %d" : "LOG GUARDADO TUBE5-DEBUG.LOG", result);
#else
    (void)stage;
    (void)position;
    (void)frames;
#endif
}

bool MediaPlayer::open(SDL_Renderer *renderer, const char *path) noexcept
{
    if (memory_blocked_)
        return false;
    close();
    error_[0] = '\0';
    failure_log_status_[0] = 0;
    begin_network_session();
    youtube_ = path && std::strncmp(path, "https://", 8) == 0;
    network_ = youtube_ || (path && std::strncmp(path, "http://", 7) == 0);
    reset_memory_diagnostic();
    source_ = youtube_ ? youtube_stream_.open(path) : Kit_CreateSourceFromUrl(path);
    tube5::finish_network_open();
    if (!source_)
        return fail("SOURCE", youtube_                         ? youtube_stream_.error()
                              : tube5::network_open_error()[0] ? tube5::network_open_error()
                                                               : Kit_GetError());
    return complete_open(renderer);
}

int MediaPlayer::open_source_worker(void *userdata) noexcept
{
    auto *self = static_cast<MediaPlayer *>(userdata);
    memory_set_stage("SOURCE_OPEN");
    self->source_ = self->youtube_ ? self->youtube_stream_.open(self->opening_path_)
                                   : Kit_CreateSourceFromUrl(self->opening_path_);
    if (self->source_ && self->opening_audio_path_[0] && !network_session_interrupted())
        self->audio_source_ = self->youtube_audio_stream_.open(self->opening_audio_path_);
    finish_network_open();
    if (!self->source_)
    {
        const char *detail = self->youtube_ ? self->youtube_stream_.error() : network_open_error();
        if (!detail[0])
            detail = Kit_GetError();
        std::snprintf(self->error_, sizeof(self->error_), "SOURCE: %s",
                      detail ? detail : "unknown error");
    }
    else if (self->opening_audio_path_[0] && !self->audio_source_)
        std::snprintf(self->error_, sizeof(self->error_), "AUDIO SOURCE: %s",
                      self->youtube_audio_stream_.error());
    // Publish all source/error writes before the UI thread adopts them.
    self->opening_done_.store(true, std::memory_order_release);
    return 0;
}

bool MediaPlayer::begin_open(const char *path, const char *audio_path) noexcept
{
    if (memory_blocked_)
        return false;
    close();
    error_[0] = '\0';
    failure_log_status_[0] = 0;
    if (!path || std::strlen(path) >= sizeof(opening_path_))
        return fail("SOURCE", "invalid or oversized URL");
    std::snprintf(opening_path_, sizeof(opening_path_), "%s", path);
    if (audio_path && *audio_path)
    {
        if (std::strlen(audio_path) >= sizeof(opening_audio_path_) ||
            std::strncmp(path, "https://", 8) != 0)
            return fail("AUDIO SOURCE", "invalid separate stream URL");
        std::snprintf(opening_audio_path_, sizeof(opening_audio_path_), "%s", audio_path);
    }
    youtube_ = std::strncmp(path, "https://", 8) == 0;
    network_ = youtube_ || std::strncmp(path, "http://", 7) == 0;
    begin_network_session();
    reset_memory_diagnostic();
    opening_cancelled_ = false;
    opening_done_ = false;
    opening_thread_ = SDL_CreateThread(open_source_worker, "tube5-open", this);
    if (!opening_thread_)
        return fail("OPEN THREAD", SDL_GetError());
    return true;
}

void MediaPlayer::cancel_open() noexcept
{
    if (!opening())
        return;
    opening_cancelled_ = true;
    cancel_network_session();
}

MediaPlayer::OpenResult MediaPlayer::poll_open(SDL_Renderer *renderer) noexcept
{
    if (!opening_thread_ || !opening_done_.load(std::memory_order_acquire))
        return OpenResult::pending;
    SDL_WaitThread(opening_thread_, nullptr);
    opening_thread_ = nullptr;
    if (opening_cancelled_)
    {
        close();
        error_[0] = '\0';
        return OpenResult::cancelled;
    }
    if (!source_ || (opening_audio_path_[0] && !audio_source_))
    {
        close();
        save_failure_report("SOURCE_OPEN", 0.0, 0);
        return OpenResult::failed;
    }
    return complete_open(renderer) ? OpenResult::ready : OpenResult::failed;
}

bool MediaPlayer::complete_open(SDL_Renderer *renderer) noexcept
{
    memory_set_stage("PLAYER_SETUP");
    if (memory_out_of_memory())
        return fail("SOURCE MEMORY", "ENOMEM - CIERRA Y REABRE TUBE5");
    const int video = Kit_GetBestSourceStream(source_, KIT_STREAMTYPE_VIDEO);
    const int audio =
        Kit_GetBestSourceStream(audio_source_ ? audio_source_ : source_, KIT_STREAMTYPE_AUDIO);
    if (video < 0 || audio < 0)
        return fail("STREAMS", "test clip must contain video and audio");
    Kit_VideoFormatRequest video_request{};
    Kit_ResetVideoFormatRequest(&video_request);
    // Keep decoded frames in planar YUV420. RGBA costs ~1 MiB per
    // converted 640x360 frame, on top of the decoder and SDL texture storage.
    video_request.format = SDL_PIXELFORMAT_IYUV;
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
    // a2 ramps FIFO batches to 8192 samples once >=16 output frames queue.
    // Keep PCM queue below that threshold, rather than retaining up to 64
    // growing blocks. Demux backpressure bounds the upstream audio packets.
    Kit_SetHint(KIT_HINT_AUDIO_BUFFER_FRAMES, 8);
    Kit_SetHint(KIT_HINT_AUDIO_BUFFER_PACKETS, 16);
    reset_codec_diagnostic();
    memory_set_stage("PLAYER_CREATE");
    if (audio_source_)
    {
        const double video_duration = Kit_GetSourceDuration(source_);
        const double audio_duration = Kit_GetSourceDuration(audio_source_);
        if (!std::isfinite(video_duration) || !std::isfinite(audio_duration) ||
            video_duration <= 0 || std::abs(video_duration - audio_duration) > 2.0)
            return fail("STREAMS", "audio/video duration mismatch");
        (void)create_separate_players(source_, video, audio_source_, audio, &video_request,
                                      &audio_request, player_, audio_player_);
    }
    else
        player_ =
            Kit_CreatePlayer(source_, video, audio, -1, &video_request, &audio_request, 1920, 1080);
    if (!player_ || (audio_source_ && !audio_player_))
        return fail("DECODER", codec_diagnostic()[0] ? codec_diagnostic() : Kit_GetError());
    if (memory_out_of_memory())
        return fail("DECODER MEMORY", "ENOMEM - CIERRA Y REABRE TUBE5");
    Kit_PlayerInfo info{};
    Kit_GetPlayerInfo(player_, &info);
    if (audio_player_)
    {
        Kit_PlayerInfo audio_info{};
        Kit_GetPlayerInfo(audio_player_, &audio_info);
        info.audio_format = audio_info.audio_format;
    }
    if (info.video_format.format != SDL_PIXELFORMAT_IYUV)
        return fail("VIDEO", "raw frame path requires IYUV");
    width_ = info.video_format.width;
    height_ = info.video_format.height;
    frame_area_ = SDL_Rect{0, 0, width_, height_};
    if (width_ <= 0 || height_ <= 0 || width_ > 1920 || height_ > 1080)
        return fail("VIDEO", "unsupported test frame size");
    memory_set_stage("TEXTURE_CREATE");
    // SDL's software IYUV fallback allocates a full RGB scratch image for
    // every STATIC update. STREAMING converts into the existing native pixels.
    texture_ = SDL_CreateTexture(renderer, info.video_format.format, SDL_TEXTUREACCESS_STREAMING,
                                 width_, height_);
    if (!texture_)
        return fail("TEXTURE", SDL_GetError());
    if (SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE) != 0)
        return fail("TEXTURE BLEND", SDL_GetError());
    SDL_AudioSpec desired{};
    desired.freq = info.audio_format.sample_rate;
    desired.format = static_cast<SDL_AudioFormat>(info.audio_format.format);
    desired.channels = static_cast<Uint8>(info.audio_format.channels);
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = this;
    memory_set_stage("AUDIO_OPEN");
    audio_ = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!audio_)
        return fail("AUDIO", SDL_GetError());
    reset_thread_diagnostic();
    begin_pipeline_diagnostic();
    Kit_PlayerPlay(player_);
    if (audio_player_)
        Kit_PlayerPlay(audio_player_);
    memory_set_stage("PLAYING");
    if (thread_diagnostic()[0])
        return fail("THREAD", thread_diagnostic());
    // The first draw starts audio, after the UI has scheduled any opening skip.
    last_frame_at_ = SDL_GetTicks();
    return true;
}

void MediaPlayer::audio_callback(void *userdata, Uint8 *stream, int length) noexcept
{
    SDL_memset(stream, 0, static_cast<std::size_t>(length));
    auto *self = static_cast<MediaPlayer *>(userdata);
    if (self->player_)
    {
        ++self->audio_callbacks_;
        int filled = 0;
        while (filled < length)
        {
            const int result =
                Kit_GetPlayerAudioData(self->audio_player_ ? self->audio_player_ : self->player_,
                                       static_cast<std::size_t>(filled), stream + filled,
                                       static_cast<std::size_t>(length - filled));
            if (result <= 0)
            {
                if (result < 0)
                    self->audio_error_ = result;
                ++self->audio_underruns_;
                break;
            }
            filled += result;
            self->audio_bytes_ += static_cast<std::size_t>(result);
            memory_mark_playback_output(false);
        }
        // Keep consuming PCM to initialize the primary clock, without letting
        // the sponsored opening (or its seek preroll) reach the speakers.
        if (self->initial_seek_muted_.load())
            SDL_memset(stream, 0, static_cast<std::size_t>(length));
    }
}

void MediaPlayer::close() noexcept
{
    // Interrupt blocked HTTP reads before joining any worker. UI cancellation
    // uses poll_open so it never joins a still-running opening thread.
    cancel_network_session();
    if (opening_thread_)
        SDL_WaitThread(opening_thread_, nullptr);
    opening_thread_ = nullptr;
    opening_done_ = false;
    opening_cancelled_ = false;
    // Stop and join the callback before releasing the decoder it reads.
    if (audio_)
        SDL_CloseAudioDevice(audio_);
    audio_ = 0;
    reset_audio_reader();
    if (audio_player_)
        Kit_ClosePlayer(audio_player_);
    audio_player_ = nullptr;
    if (player_)
        Kit_ClosePlayer(player_);
    end_pipeline_diagnostic();
    player_ = nullptr;
    if (texture_)
        SDL_DestroyTexture(texture_);
    texture_ = nullptr;
    if (source_ && !youtube_)
        Kit_CloseSource(source_);
    youtube_stream_.close();
    youtube_audio_stream_.close();
    audio_source_ = nullptr;
    opening_audio_path_[0] = 0;
    source_ = nullptr;
    paused_ = false;
    frames_uploaded_ = 0;
    frames_presented_ = 0;
    raw_attempts_ = raw_successes_ = 0;
    raw_result_ = 0;
    audio_callbacks_ = 0;
    audio_bytes_ = 0;
    audio_error_ = 0;
    audio_underruns_ = 0;
    seek_requests_ = 0;
    seek_dispatches_ = 0;
    frame_area_ = {};
    paused_position_ = 0.0;
    pending_seek_ = -1.0;
    initial_seek_target_ = -1.0;
    initial_seek_muted_ = false;
    audio_started_ = false;
    seek_dispatched_at_ = 0;
    network_ = false;
    youtube_ = false;
}

void MediaPlayer::toggle_pause() noexcept
{
    if (!player_)
        return;
    const bool was_paused = paused();
    SDL_PauseAudioDevice(audio_, 1);
    if (was_paused)
    {
        Kit_PlayerPlay(player_);
        if (audio_player_)
            Kit_PlayerPlay(audio_player_);
    }
    else
    {
        paused_position_ = Kit_GetPlayerPosition(player_);
        Kit_PlayerPause(player_);
        if (audio_player_)
            Kit_PlayerPause(audio_player_);
    }
    paused_ = !was_paused;
    if (was_paused)
    {
        last_frame_at_ = SDL_GetTicks();
        SDL_PauseAudioDevice(audio_, 0);
    }
}

void MediaPlayer::seek(double delta) noexcept
{
    if (!player_ || !std::isfinite(delta))
        return;
    const double base = pending_seek_ >= 0.0 ? pending_seek_ : position();
    seek_to(base + delta);
}

void MediaPlayer::seek_to(double destination) noexcept
{
    if (!player_ || !std::isfinite(destination) || !std::isfinite(duration()))
        return;
    // An explicit user seek replaces an in-flight sponsor destination too.
    initial_seek_target_ = -1.0;
    initial_seek_muted_ = false;
    ++seek_requests_;
    const bool was_paused = paused();
    const double target = std::clamp(destination, 0.0, std::max(0.0, duration() - 0.1));
    pending_seek_ = target;
    seek_requested_at_ = SDL_GetTicks();
    last_frame_at_ = seek_requested_at_;
    if (was_paused)
        paused_position_ = target;
}

void MediaPlayer::seek_sponsor(double destination) noexcept
{
    if (!player_ || !std::isfinite(destination))
        return;
    seek_to(destination);
    initial_seek_target_ = pending_seek_;
    initial_seek_muted_ = initial_seek_target_ >= 0.0;
}

void MediaPlayer::draw(SDL_Renderer *renderer, const SDL_Rect &destination) noexcept
{
    if (!player_ || !texture_)
        return;
    if (!audio_started_ && !paused_)
    {
        SDL_PauseAudioDevice(audio_, 0);
        audio_started_ = true;
    }
    // a2 silently drops a seek if the demuxer already has one pending. Coalesce
    // quick button presses, and defer paused seeks until buffers can drain.
    const Uint32 now = SDL_GetTicks();
    if (!paused_ && pending_seek_ >= 0.0 && now - seek_requested_at_ >= 100 &&
        (seek_dispatched_at_ == 0 || now - seek_dispatched_at_ >= 500))
    {
        SDL_PauseAudioDevice(audio_, 1);
        reset_audio_reader();
        const int video_seek = Kit_PlayerSeek(player_, pending_seek_);
        const int audio_seek = audio_player_ ? Kit_PlayerSeek(audio_player_, pending_seek_) : 0;
        if (video_seek != 0 || audio_seek != 0)
        {
            std::snprintf(error_, sizeof(error_), "SEEK: %s", Kit_GetError());
            initial_seek_target_ = -1.0;
            initial_seek_muted_ = false;
        }
        else
            ++seek_dispatches_;
        pending_seek_ = -1.0;
        seek_dispatched_at_ = now;
        SDL_PauseAudioDevice(audio_, 0);
    }
    // Kitchensink's SDL texture helper ignores SDL_UpdateTexture's result.
    // Read the three YUV planes ourselves and keep them locked until the copy finishes.
    if (!paused_ && Kit_GetPlayerState(player_) == KIT_PLAYING)
    {
        unsigned char **data = nullptr;
        int *pitch = nullptr;
        SDL_Rect area{};
        ++raw_attempts_;
        raw_result_ = Kit_LockPlayerVideoRawFrame(player_, &data, &pitch, &area);
        if (raw_result_ == 0)
        {
            ++raw_successes_;
            const bool valid = data && pitch && data[0] && area.x == 0 && area.y == 0 &&
                               area.w > 0 && area.h > 0 && area.w <= width_ && area.h <= height_ &&
                               data[1] && data[2] && pitch[0] >= area.w &&
                               pitch[1] >= (area.w + 1) / 2 && pitch[2] >= (area.w + 1) / 2;
            if (!valid)
                std::snprintf(error_, sizeof(error_), "VIDEO FRAME: invalid IYUV layout");
            else if (SDL_UpdateYUVTexture(texture_, &area, data[0], pitch[0], data[1], pitch[1],
                                          data[2], pitch[2]) != 0)
                std::snprintf(error_, sizeof(error_), "VIDEO UPLOAD: %s", SDL_GetError());
            else
            {
                frame_area_ = area;
                ++frames_uploaded_;
                if (initial_seek_target_ >= 0.0 && pending_seek_ < 0.0 &&
                    Kit_GetPlayerPosition(player_) >= initial_seek_target_)
                {
                    initial_seek_target_ = -1.0;
                    initial_seek_muted_ = false;
                }
                memory_mark_playback_output(true);
                last_frame_at_ = now;
                // Do not sleep for a diagnostic notification here. The boot
                // checkpoint waits 700 ms, making following frames late.
            }
            Kit_UnlockPlayerVideoRawFrame(player_);
        }
    }
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
    if (frames_uploaded_ && initial_seek_target_ < 0.0)
    {
        if (SDL_RenderCopy(renderer, texture_, &frame_area_, &target) != 0)
            std::snprintf(error_, sizeof(error_), "VIDEO DRAW: %s", SDL_GetError());
        else
            ++frames_presented_;
    }
}

bool MediaPlayer::check_network_failure() noexcept
{
    if (!network_ || !player_ || opening())
        return false;
    if (memory_out_of_memory())
    {
        memory_blocked_ = true;
        fail("MEMORY", "ENOMEM - CIERRA Y REABRE TUBE5");
        return true;
    }
    const int read_error = network_playback_error();
    if (read_error < 0)
    {
        if (read_error == AVERROR(ENOMEM))
        {
            memory_blocked_ = true;
            fail("HTTP READ", "ENOMEM - CIERRA Y REABRE TUBE5");
            return true;
        }
        char detail[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(read_error, detail, sizeof(detail));
        fail(youtube_ ? "YT READ" : "HTTP READ",
             youtube_ && youtube_stream_.error()[0]         ? youtube_stream_.error()
             : youtube_ && youtube_audio_stream_.error()[0] ? youtube_audio_stream_.error()
                                                            : detail);
        return true;
    }
    if (finished() && position() + 2.0 < duration())
    {
        fail("HTTP", "video ended before expected duration");
        return true;
    }
    if (!paused_ && !finished() && SDL_GetTicks() - last_frame_at_ >= 8000)
    {
        fail("HTTP", "no video frames for 8 seconds - retry");
        return true;
    }
    return false;
}

void MediaPlayer::diagnostic_status(char *text, std::size_t capacity) const noexcept
{
    unsigned frames = 0, frame_capacity = 0, packets = 0, packet_capacity = 0;
    if (player_)
        Kit_GetPlayerVideoBufferState(player_, &frames, &frame_capacity, &packets,
                                      &packet_capacity);
    std::snprintf(text, capacity,
                  "FR %u BUF %u/%u PKT %u/%u SEEK %u/%u RAW %u/%u RC%d S%d AUD %u/%zu U%u E%d",
                  frames_uploaded_, frames, frame_capacity, packets, packet_capacity,
                  seek_dispatches_, seek_requests_, raw_successes_, raw_attempts_, raw_result_,
                  player_ ? Kit_GetPlayerState(player_) : KIT_CLOSED, audio_callbacks_.load(),
                  audio_bytes_.load(), audio_underruns_.load(), audio_error_.load());
}

double MediaPlayer::position() const noexcept
{
    // Kitchensink a2 keeps its clock advancing while paused. Present our frozen
    // timestamp until it resynchronizes to decoded PTS after resume.
    return player_ ? (paused_            ? paused_position_
                      : frames_uploaded_ ? Kit_GetPlayerPosition(player_)
                                         : 0.0)
                   : 0.0;
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
