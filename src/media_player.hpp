// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <kitchensink2/kitchensink.h>

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
    void close() noexcept;
    void toggle_pause() noexcept;
    void seek(double delta) noexcept;
    void draw(SDL_Renderer *renderer, const SDL_Rect &destination) noexcept;
    double position() const noexcept;
    double duration() const noexcept;
    bool paused() const noexcept;
    bool finished() const noexcept;
    const char *error() const noexcept
    {
        return error_;
    }

  private:
    static void audio_callback(void *userdata, Uint8 *stream, int length) noexcept;
    bool fail(const char *stage, const char *detail) noexcept;
    Kit_Source *source_ = nullptr;
    Kit_Player *player_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_AudioDeviceID audio_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool paused_ = false;
    double paused_position_ = 0.0;
    double pending_seek_ = -1.0;
    Uint32 seek_requested_at_ = 0;
    Uint32 seek_dispatched_at_ = 0;
    char error_[256]{};
};
} // namespace tube5
