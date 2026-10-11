// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <kitchensink2/kitchensink.h>
#include <atomic>
#include <cstddef>

namespace tube5
{
class MediaPlayer final
{
  public:
    ~MediaPlayer()
    {
        close();
    }
    MediaPlayer() = default;
    MediaPlayer(const MediaPlayer &) = delete;
    MediaPlayer &operator=(const MediaPlayer &) = delete;

    bool open(SDL_Renderer *renderer, const char *path) noexcept;
    enum class OpenResult
    {
        pending,
        ready,
        failed,
        cancelled
    };
    bool begin_open(const char *path) noexcept;
    OpenResult poll_open(SDL_Renderer *renderer) noexcept;
    void cancel_open() noexcept;
    bool opening() const noexcept
    {
        return opening_thread_ != nullptr;
    }
    bool cancelling() const noexcept
    {
        return opening_cancelled_;
    }
    // Called on the UI thread; stops playback on a network error or stalled frames.
    bool check_network_failure() noexcept;
    void close() noexcept;
    void toggle_pause() noexcept;
    void seek(double delta) noexcept;
    // Replace a pending relative seek with this absolute destination.
    void seek_to(double destination) noexcept;
    void draw(SDL_Renderer *renderer, const SDL_Rect &destination) noexcept;
    double position() const noexcept;
    double duration() const noexcept;
    bool paused() const noexcept;
    bool seeking() const noexcept
    {
        return pending_seek_ >= 0.0;
    }
    bool finished() const noexcept;
    void diagnostic_status(char *text, std::size_t capacity) const noexcept;
    unsigned frames_uploaded() const noexcept
    {
        return frames_uploaded_;
    }
    const char *error() const noexcept
    {
        return error_;
    }

  private:
    static int open_source_worker(void *userdata) noexcept;
    bool complete_open(SDL_Renderer *renderer) noexcept;
    SDL_Thread *opening_thread_ = nullptr;
    std::atomic<bool> opening_done_{false};
    bool opening_cancelled_ = false;
    bool network_ = false;
    // A native ENOMEM followed by repeated opens has crashed on hardware.
    // Preserve the first failure and require a fresh app session, not retries.
    bool memory_blocked_ = false;
    Uint32 last_frame_at_ = 0;
    char opening_path_[256]{};
    static void audio_callback(void *userdata, Uint8 *stream, int length) noexcept;
    bool fail(const char *stage, const char *detail) noexcept;
    Kit_Source *source_ = nullptr;
    Kit_Player *player_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_AudioDeviceID audio_ = 0;
    int width_ = 0;
    int height_ = 0;
    SDL_Rect frame_area_{};
    unsigned frames_uploaded_ = 0;
    unsigned raw_attempts_ = 0;
    unsigned raw_successes_ = 0;
    int raw_result_ = 0;
    std::atomic<unsigned> audio_callbacks_{0};
    std::atomic<std::size_t> audio_bytes_{0};
    std::atomic<int> audio_error_{0};
    std::atomic<unsigned> audio_underruns_{0};
    unsigned seek_requests_ = 0;
    unsigned seek_dispatches_ = 0;
    bool paused_ = false;
    double paused_position_ = 0.0;
    double pending_seek_ = -1.0;
    Uint32 seek_requested_at_ = 0;
    Uint32 seek_dispatched_at_ = 0;
    char error_[256]{};
};
} // namespace tube5
