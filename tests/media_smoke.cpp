// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "media_player.hpp"
#include "pipeline_diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError());
        std::exit(1);
    }
}

int main()
{
    SDL_SetMainReady();
    require(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_AUDIO) == 0, "SDL init without video driver");
    require(Kit_Init(0) == 0, "Kit init");
    SDL_Surface *output = SDL_CreateRGBSurfaceWithFormat(0, 640, 360, 32, SDL_PIXELFORMAT_RGBA32);
    require(output != nullptr, "offscreen surface");
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(output);
    require(renderer != nullptr, "renderer");
    const auto frame = [&]()
    {
        // Match Tube5's direct UI painting followed by a video overlay.
        require(SDL_LockSurface(output) == 0, "direct UI surface lock");
        auto *pixels = static_cast<Uint32 *>(output->pixels);
        for (int y = 0; y < output->h; ++y)
            std::fill_n(pixels + y * (output->pitch / 4), output->w, 0xff101010);
        SDL_UnlockSurface(output);
    };
    tube5::MediaPlayer player;
    require(!player.open(renderer, "assets/does-not-exist.mp4"), "missing file rejected");
    require(player.error()[0] != '\0', "missing file diagnostic");
    require(player.open(renderer, "assets/test-clip.mp4"), player.error());
    require(Kit_GetHint(KIT_HINT_THREAD_COUNT) == 1, "explicit single-thread codec baseline");
    require(std::abs(player.duration() - 20.0) < 0.2, "clip duration");
    // Exercise uninterrupted playback beyond the ~7s PS5 crash observed in
    // BOOT-DIAG-10 before testing pause/seeks. The dummy backend is not PS5.
    for (int i = 0; i < 300; ++i)
    {
        frame();
        player.draw(renderer, SDL_Rect{20, 20, 600, 320});
        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }
    require(player.position() > 8.0, "uninterrupted playback advances beyond eight seconds");
    require(player.frames_uploaded() > 100, "IYUV frames continue uploading beyond startup");
    require(!player.error()[0], "no hidden frame upload/draw errors");
    char pipeline[128]{};
    unsigned sent = 0, decoded = 0;
    int send_error = 0, decode_error = 0;
    tube5::pipeline_diagnostic_status(1, pipeline, sizeof(pipeline));
    require(std::sscanf(pipeline, "VSEND %u VDEC %u ERR %d/%d", &sent, &decoded, &send_error,
                        &decode_error) == 4 &&
                sent > 0 && decoded >= player.frames_uploaded() && send_error == 0 &&
                decode_error == 0,
            "live FFmpeg pipeline counters");
    // Capture pixels and prove actual decoded frames are being displayed.
    SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(0, 640, 360, 32, SDL_PIXELFORMAT_RGBA32);
    require(capture != nullptr, "capture allocation");
    require(SDL_RenderReadPixels(renderer, nullptr, capture->format->format, capture->pixels,
                                 capture->pitch) == 0,
            "video capture");
    auto *bytes = static_cast<unsigned char *>(capture->pixels);
    require(*static_cast<Uint32 *>(capture->pixels) == 0xff101010,
            "direct UI pixels survive video overlay");
    int colored = 0;
    for (int i = 0; i < 640 * 360; ++i)
        colored += bytes[i * 4] > 32 || bytes[i * 4 + 1] > 32 || bytes[i * 4 + 2] > 32;
    require(colored > 20000, "decoded image is not black");
    require(SDL_SaveBMP(capture, "build/video-preview.bmp") == 0, "save video preview");
    SDL_FreeSurface(capture);
    player.toggle_pause();
    require(player.paused(), "pause");
    const double paused_at = player.position();
    SDL_Delay(150);
    require(std::abs(player.position() - paused_at) < 0.05, "paused clock remains still");
    const unsigned paused_frames = player.frames_uploaded();
    frame();
    player.draw(renderer, SDL_Rect{20, 20, 600, 320});
    SDL_RenderPresent(renderer);
    require(player.frames_uploaded() == paused_frames, "pause retains the previous frame");
    player.seek(5.0);
    require(player.paused(), "seek preserves pause");
    require(std::abs(player.position() - paused_at - 5.0) < 0.3, "forward seek");
    player.seek(-100.0);
    require(player.position() >= 0 && player.position() < 0.3, "negative seek clamps to zero");
    player.toggle_pause();
    require(!player.paused(), "resume");
    for (int i = 0; i < 30; ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        SDL_Delay(33);
    }
    require(player.position() < 2.0, "paused seek applied on resume");
    const double forward_from = player.position();
    player.seek(5.0);
    for (int i = 0; i < 60 && player.position() < forward_from + 4.0; ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }
    require(player.position() >= forward_from + 4.0, "playing forward seek actually changes clock");
    const double backward_from = player.position();
    player.seek(-5.0);
    for (int i = 0; i < 60 && player.position() > backward_from - 4.0; ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }
    require(player.position() <= backward_from - 4.0,
            "playing backward seek actually changes clock");
    require(!player.error()[0], "no hidden seek errors");
    unsigned lock_errors = 0, seeks = 0;
    int seek_error = 0;
    tube5::pipeline_diagnostic_status(2, pipeline, sizeof(pipeline));
    require(std::sscanf(pipeline, "LOCKFAIL %u SEEKEXEC %u ERR %d", &lock_errors, &seeks,
                        &seek_error) == 3 &&
                seeks >= 3 && seek_error == 0,
            "seeks executed by the demuxer, not just requested");
    player.seek(100.0);
    for (int i = 0; i < 90 && !player.finished(); ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        SDL_Delay(33);
    }
    std::printf("EOF position %.3f / %.3f\n", player.position(), player.duration());
    require(player.finished(), "end of video");
    player.close();
    require(player.open(renderer, "assets/test-clip.mp4"), "reopen after end");
    player.close();
    player.close();
    Kit_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(output);
    SDL_Quit();
    std::puts("Media smoke passed: open, decode, pause, seek, EOF, reopen, cleanup.");
}
