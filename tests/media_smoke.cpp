// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "media_player.hpp"
#include "sponsor_skip.hpp"
#include "lan_catalog.hpp"
#include "pipeline_diagnostics.hpp"
#include "youtube_probe.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C"
{
#include <libavutil/error.h>
    int __real__ZN5tube522network_playback_errorEv();
    bool __real__ZN5tube520memory_out_of_memoryEv();
}
static bool inject_read_oom = false;
static bool inject_allocator_oom = false;
extern "C" bool __wrap__ZN5tube520memory_out_of_memoryEv()
{
    return inject_allocator_oom || __real__ZN5tube520memory_out_of_memoryEv();
}
// Host-test-only link wrapper: exercise teardown/guard without exhausting RAM.
extern "C" int __wrap__ZN5tube522network_playback_errorEv()
{
    return inject_read_oom ? AVERROR(ENOMEM) : __real__ZN5tube522network_playback_errorEv();
}

static void require(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError());
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool youtube = argc > 1 && std::strcmp(argv[1], "--youtube-direct") == 0;
    const bool native_sponsor = (argc > 2 && std::strcmp(argv[2], "--sponsor-native") == 0) ||
                                (argc > 4 && std::strcmp(argv[4], "--sponsor-native") == 0);
    const char *clip = argc > 1 ? argv[1] : "assets/test-clip.mp4";
    const bool real_video = youtube || (argc > 2 && std::strcmp(argv[2], "--real-video") == 0);
    SDL_SetMainReady();
    require(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_AUDIO) == 0, "SDL init without video driver");
    require(Kit_Init(KIT_INIT_NETWORK) == 0, "Kit init with network");
    tube5::YouTubeProbe probe;
    if (youtube)
    {
        const char *input =
            argc > 2 && std::strncmp(argv[2], "--", 2) != 0 ? argv[2] : "bHIhgxav9LY";
        const unsigned height =
            argc > 3 ? static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10)) : 0;
        require(probe.begin(tube5::ProbeMode::formats, input, height), "launch direct resolver");
        const auto started = SDL_GetTicks64();
        while (probe.loading())
        {
            probe.poll();
            SDL_Delay(10);
            require(SDL_GetTicks64() - started < 32000, "bounded direct resolver");
        }
        require(probe.result().source_mp4, probe.result().error.data());
        clip = probe.result().formats.source.data(); // Never print the signed URL.
    }
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
    const auto async_open = [&](const char *path, bool cancel, unsigned cancel_delay = 0)
    {
        const Uint64 started = SDL_GetTicks64();
        require(
            player.begin_open(path, youtube ? probe.result().formats.audio_source.data() : nullptr),
            "launch source worker");
        require(SDL_GetTicks64() - started < 250, "opening returns without waiting for network");
        if (cancel)
        {
            for (unsigned elapsed = 0; elapsed < cancel_delay; elapsed += 16)
            {
                frame();
                SDL_PumpEvents();
                SDL_RenderPresent(renderer);
                SDL_Delay(16);
            }
            player.cancel_open();
        }
        unsigned responsive_frames = 0;
        tube5::MediaPlayer::OpenResult result;
        do
        {
            const Uint64 tick = SDL_GetTicks64();
            frame();
            SDL_PumpEvents();
            result = player.poll_open(renderer);
            SDL_RenderPresent(renderer);
            if (result == tube5::MediaPlayer::OpenResult::pending)
                require(SDL_GetTicks64() - tick < 250, "UI tick does not wait for opening worker");
            ++responsive_frames;
            require(SDL_GetTicks64() - started < 19000, "async two-source open is bounded");
            SDL_Delay(16);
        } while (result == tube5::MediaPlayer::OpenResult::pending);
        require(responsive_frames > 0, "UI updates while source opens");
        return result;
    };
    require(!player.open(renderer, "assets/does-not-exist.mp4"), "missing file rejected");
    require(player.error()[0] != '\0', "missing file diagnostic");
    if (argc > 2 && std::strcmp(argv[2], "--expect-open-failure") == 0)
    {
        const Uint64 start = SDL_GetTicks64();
        require(async_open(clip, false) == tube5::MediaPlayer::OpenResult::failed,
                "stalled HTTP source rejected");
        require(SDL_GetTicks64() - start < 11000, "HTTP failure returns within deadline");
        require(std::strstr(player.error(), "HTTP") != nullptr, "HTTP failure detail preserved");
        require(player.open(renderer, "assets/test-clip.mp4"), "local recovery after HTTP failure");
        player.close();
        Kit_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(output);
        SDL_Quit();
        std::puts("HTTP stall passed: bounded failure and local recovery.");
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[2], "--expect-cancel") == 0)
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            require(async_open(clip, true, attempt ? 200 : 0) ==
                        tube5::MediaPlayer::OpenResult::cancelled,
                    "cancel wins even before worker starts");
            require(!player.opening() && !player.error()[0], "cancel clears opening and error");
        }
        require(player.open(renderer, "assets/test-clip.mp4"), "local recovery after cancel");
        player.close();
        Kit_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(output);
        SDL_Quit();
        std::puts("HTTP cancel passed: repeated cancellation and local recovery.");
        return 0;
    }
    if (youtube && probe.result().formats.audio_source[0])
        require(async_open(clip, true) == tube5::MediaPlayer::OpenResult::cancelled,
                "cancel separate-source opening before playback");
    if (std::strncmp(clip, "http://", 7) == 0 || youtube)
        require(async_open(clip, false) == tube5::MediaPlayer::OpenResult::ready, player.error());
    else
        require(player.open(renderer, clip), player.error());
    if (native_sponsor || (argc > 2 && (std::strcmp(argv[2], "--sponsor-test") == 0 ||
                                        std::strcmp(argv[2], "--sponsor-real") == 0 ||
                                        std::strcmp(argv[2], "--sponsor-native") == 0)))
    {
        tube5::SponsorData data;
        data.state = tube5::SponsorState::ready;
        data.ranges[0] = {0, 4000};
        const unsigned end_ms = static_cast<unsigned>(std::round(player.duration() * 1000));
        data.ranges[1] = {end_ms - 5000, end_ms};
        data.count = 2;
        if (native_sponsor)
        {
            require(youtube, "native SponsorBlock requires the direct resolver");
            data = tube5::aligned_sponsor(probe.result().sponsor, player.duration());
            require(data.state == tube5::SponsorState::ready && data.count == 2,
                    "two real API segments aligned to direct source");
        }
        if (std::strcmp(argv[2], "--sponsor-real") == 0)
        {
            tube5::LanCatalog catalog;
            require(catalog.begin(clip), "load real segment catalogue");
            const Uint64 started = SDL_GetTicks64();
            while (catalog.loading())
            {
                catalog.poll();
                require(SDL_GetTicks64() - started < 10000, "bounded real catalogue fetch");
                SDL_Delay(5);
            }
            require(!catalog.error()[0], catalog.error());
            bool found = false;
            for (unsigned i = 0; i < catalog.data().count; ++i)
            {
                char url[256]{};
                if (catalog.media_url(i, url, sizeof(url)) && std::strcmp(url, clip) == 0)
                {
                    data = catalog.data().entries[i].sponsor;
                    found = true;
                }
            }
            require(found && data.state == tube5::SponsorState::ready && data.count == 2,
                    "real video has two ready segments");
        }
        tube5::SponsorSkip skip;
        skip.reset(data);
        const auto opening_target = skip.target(0.0, player.duration(), true, false, false);
        require(opening_target && *opening_target == 4.0, "known opening segment");
        player.seek_sponsor(*opening_target);
        const Uint64 start = SDL_GetTicks64();
        bool moved_to_outro = false;
        bool first_presented = false;
        bool sponsor_ended = false;
        while (!player.finished())
        {
            require(SDL_GetTicks64() - start < 12000, "bounded automatic skip test");
            require(!player.check_network_failure(), "healthy SponsorBlock HTTP playback");
            if (const auto target =
                    skip.target(player.position(), player.duration(), true, player.paused(),
                                player.seeking() || player.frames_uploaded() == 0))
            {
                std::printf("Sponsor request %.3f -> %.3f (%u)\n", player.position(), *target,
                            skip.skipped());
                if (*target >= player.duration())
                {
                    sponsor_ended = true;
                    break;
                }
                player.seek_sponsor(*target);
            }
            player.draw(renderer, SDL_Rect{0, 0, 640, 360});
            if (!first_presented && player.frames_presented())
            {
                require(player.position() >= 4.0, "no opening sponsor frame presented");
                first_presented = true;
            }
            if (!moved_to_outro && player.frames_uploaded() > 0 && player.position() > 4.5 &&
                player.position() < player.duration() - 5 && !player.seeking())
            {
                std::printf("Sponsor first position %.3f count %u\n", player.position(),
                            skip.skipped());
                require(skip.skipped() == 1, "initial segment skipped exactly once");
                player.toggle_pause();
                require(!skip.target(player.position(), player.duration(), true, true, false),
                        "pause suppresses automatic seeks");
                player.toggle_pause();
                skip.manual_seek();
                player.seek_to(data.ranges[1].start_ms / 1000.0 + 0.5);
                moved_to_outro = true;
            }
            SDL_Delay(16);
        }
        require(skip.skipped() == 2, "end segment skipped exactly once");
        require(moved_to_outro, "manual seek preserved");
        require(first_presented && sponsor_ended, "clean opening and immediate sponsor ending");
        player.close();
        require(player.open(renderer, clip), "reopen after sponsor EOF");
        player.close();
        Kit_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(output);
        SDL_Quit();
        std::puts(
            "Sponsor smoke passed: initial skip, pause, manual seek, end skip, EOF and reopen.");
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[2], "--expect-read-failure") == 0)
    {
        const Uint64 started = SDL_GetTicks64();
        unsigned frames_seen = 0;
        bool failed = false;
        while (SDL_GetTicks64() - started < 15000)
        {
            frame();
            player.draw(renderer, SDL_Rect{0, 0, 640, 360});
            frames_seen = std::max(frames_seen, player.frames_uploaded());
            SDL_RenderPresent(renderer);
            if (player.check_network_failure())
            {
                failed = true;
                break;
            }
            SDL_Delay(16);
        }
        require(frames_seen > 10, "network cut occurs after visible playback");
        require(failed && std::strstr(player.error(), "HTTP"), "network cut reports an HTTP error");
        require(player.open(renderer, "assets/test-clip.mp4"), "local recovery after network cut");
        player.close();
        Kit_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(output);
        SDL_Quit();
        std::puts("HTTP cut passed: visible playback, bounded failure and local recovery.");
        return 0;
    }
    if (argc > 2 && (std::strcmp(argv[2], "--expect-read-oom") == 0 ||
                     std::strcmp(argv[2], "--expect-allocator-oom") == 0))
    {
        for (int i = 0; i < 40; ++i)
        {
            player.draw(renderer, SDL_Rect{0, 0, 640, 360});
            SDL_Delay(16);
        }
        require(player.frames_uploaded() > 0, "playback before injected ENOMEM");
        inject_allocator_oom = std::strcmp(argv[2], "--expect-allocator-oom") == 0;
        inject_read_oom = !inject_allocator_oom;
        require(player.check_network_failure(), "read ENOMEM handled");
        inject_read_oom = false;
        inject_allocator_oom = false;
        for (int retry = 0; retry < 3; ++retry)
        {
            require(!player.begin_open(clip), "blocked repeated HTTP opens after ENOMEM");
            require(!player.open(renderer, "assets/test-clip.mp4"),
                    "blocked local open in failed session");
            require(!player.opening() && std::strstr(player.error(), "ENOMEM"),
                    "preserve original error without starting worker");
        }
        player.close();
        tube5::MediaPlayer fresh_session;
        require(fresh_session.open(renderer, "assets/test-clip.mp4"), "fresh session can recover");
        fresh_session.close();
        Kit_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(output);
        SDL_Quit();
        std::puts("HTTP OOM passed: teardown, blocked retries and fresh-session recovery.");
        return 0;
    }
    require(Kit_GetHint(KIT_HINT_THREAD_COUNT) == 1, "explicit single-thread codec baseline");
    require(Kit_GetHint(KIT_HINT_AUDIO_BUFFER_FRAMES) == 8, "bounded PCM queue");
    require(Kit_GetHint(KIT_HINT_AUDIO_BUFFER_PACKETS) == 16, "bounded audio packet queue");
    require(real_video ? player.duration() >= 20 && player.duration() <= 3600
                       : std::abs(player.duration() - 20.0) < 0.2,
            "clip duration");
    // Exercise uninterrupted playback beyond the ~7s PS5 crash observed in
    // BOOT-DIAG-10 before testing pause/seeks. The dummy backend is not PS5.
    for (int i = 0; i < 300; ++i)
    {
        frame();
        player.draw(renderer, SDL_Rect{20, 20, 600, 320});
        require(!player.check_network_failure(), "healthy HTTP playback is not a network failure");
        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }
    require(player.position() > 8.0, "uninterrupted playback advances beyond eight seconds");
    require(player.frames_uploaded() > 100, "IYUV frames continue uploading beyond startup");
    char player_status[256]{};
    player.diagnostic_status(player_status, sizeof(player_status));
    const auto *audio_status = std::strstr(player_status, "AUD ");
    unsigned audio_callbacks = 0;
    std::size_t audio_bytes = 0;
    require(audio_status &&
                std::sscanf(audio_status, "AUD %u/%zu", &audio_callbacks, &audio_bytes) == 2 &&
                audio_bytes > 100000,
            "decoded audio reaches the callback alongside video");
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
    player.seek(5.0);
    player.seek_to(0.5);
    require(player.paused(), "absolute seek preserves pause");
    require(std::abs(player.position() - 0.5) < 0.01,
            "absolute selection replaces a pending relative seek");
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
    if (player.position() < forward_from + 4.0)
    {
        tube5::pipeline_diagnostic_status(2, pipeline, sizeof(pipeline));
        std::printf("Forward seek from %.3f target %.3f reached %.3f: %s (%s)\n", forward_from,
                    forward_from + 5.0, player.position(), pipeline, player.error());
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
    if (youtube && argc > 4 && std::strcmp(argv[4], "--seek-stress") == 0)
    {
        require(player.duration() > 610, "seek stress requires a sufficiently long video");
        for (unsigned step = 0; step < 7; ++step)
        {
            double target = 0;
            if (step == 1)
            {
                target = player.position() + 30;
                player.seek(30);
            }
            else if (step == 2)
            {
                target = player.position() + 15;
                for (unsigned press = 0; press < 3; ++press)
                    player.seek(5);
            }
            else
            {
                constexpr double destinations[] = {30, 0, 0, 300, 600, 30, 0};
                target = destinations[step];
                player.seek_to(target);
            }
            const auto before = player.frames_uploaded();
            const auto started = SDL_GetTicks64();
            bool reached = false;
            while (SDL_GetTicks64() - started < 12000)
            {
                player.draw(renderer, SDL_Rect{0, 0, 640, 360});
                SDL_RenderPresent(renderer);
                if (player.check_network_failure())
                {
                    std::fprintf(stderr, "Seek stress step %u failed: %s\n", step, player.error());
                    break;
                }
                if (SDL_GetTicks64() - started >= 250 && !player.seeking() &&
                    player.frames_uploaded() >= before + 2 &&
                    player.position() >= target - 0.5 && player.position() < target + 4)
                {
                    reached = true;
                    break;
                }
                SDL_Delay(16);
            }
            std::printf("Seek stress step %u target %.3f reached %.3f frames %u ms %llu\n",
                        step, target, player.position(), player.frames_uploaded(),
                        static_cast<unsigned long long>(SDL_GetTicks64() - started));
            require(reached && !player.error()[0], "stress seek reaches decoded video without HTTP failure");
        }
    }
    unsigned lock_errors = 0, seeks = 0;
    int seek_error = 0;
    tube5::pipeline_diagnostic_status(2, pipeline, sizeof(pipeline));
    require(std::sscanf(pipeline, "LOCKFAIL %u SEEKEXEC %u ERR %d", &lock_errors, &seeks,
                        &seek_error) == 3 &&
                seeks >= 3 && seek_error == 0,
            "seeks executed by the demuxer, not just requested");
    player.seek(player.duration());
    // Arbitrary real-video GOPs can land several seconds before the target.
    // Keep the synthetic fixture's deadline; allow bounded catch-up for HD.
    const int eof_ticks = real_video ? 300 : 90;
    for (int i = 0; i < eof_ticks && !player.finished(); ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        SDL_Delay(33);
    }
    std::printf("EOF position %.3f / %.3f\n", player.position(), player.duration());
    require(player.finished(), "end of video");
    if (youtube)
    {
        player.youtube_status(pipeline, sizeof(pipeline));
        std::puts(pipeline);
    }
    player.close();
    require(youtube ? async_open(clip, false) == tube5::MediaPlayer::OpenResult::ready
                    : player.open(renderer, clip),
            "reopen after end with both sources");
    for (unsigned i = 0; i < 90 && player.frames_uploaded() < 10; ++i)
    {
        player.draw(renderer, SDL_Rect{0, 0, 640, 360});
        require(!player.check_network_failure(), "healthy reopened playback");
        SDL_Delay(33);
    }
    require(player.frames_uploaded() >= 10, "reopened video actually decodes");
    player.close();
    player.close();
    Kit_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(output);
    SDL_Quit();
    std::puts("Media smoke passed: open, decode, pause, seek, EOF, reopen, cleanup.");
}
