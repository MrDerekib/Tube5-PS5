// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "boot_diagnostics.hpp"
#include "demo_renderer.hpp"
#include "media_player.hpp"
#include "navigation.hpp"
#include "native_sdl.hpp"
#include "pipeline_diagnostics.hpp"
#include "memory_diagnostics.hpp"
#include "native_media_buffers.hpp"
#include "native_surface_buffers.hpp"
#include "native_refstruct_buffers.hpp"
#include "native_framebuffer.hpp"
#include "playback_progress.hpp"
#include "seek_selection.hpp"
#include "lan_catalog.hpp"
#include "video_sync_diagnostics.hpp"
#include "video_conversion.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

#ifndef TUBE5_HOST
extern "C" int sceSystemServiceHideSplashScreen();
extern "C" int sceUserServiceInitialize(const void *);
extern "C" int sceKernelUsleep(std::uint32_t);
#endif

namespace
{
using ps5::demo::Canvas;
using ps5::demo::Color;
constexpr auto ink = static_cast<Color>(0xff1c1610);
constexpr auto panel = static_cast<Color>(0xff382c21);
constexpr auto accent = static_cast<Color>(0xff8261ff);
constexpr auto muted = static_cast<Color>(0xffbbaea5);
std::array<char, 48> build_label{};
std::array<char, 256> network_url{};
bool network_test = true;
bool sponsor_enabled = true;
const char *sponsor_state_text(tube5::SponsorState state) noexcept
{
    switch (state)
    {
    case tube5::SponsorState::ready:
        return "SEGMENTOS LISTOS";
    case tube5::SponsorState::pending:
        return "PENDIENTE - OPTIONS RECARGAR";
    case tube5::SponsorState::unavailable:
        return "NO DISPONIBLE - SIN SALTOS";
    case tube5::SponsorState::unaligned:
        return "RECORTE O TIEMPOS NO VERIFICADOS";
    default:
        return "SIN SEGMENTOS";
    }
}
#ifdef TUBE5_DIAGNOSTICS
std::array<char, 128> frame_timing{};
#endif

void paint(Canvas &canvas, const tube5::Navigation &nav, tube5::MediaPlayer &media, bool connected,
           const char *error, const tube5::SeekSelection &selection,
           const tube5::LanCatalog &catalog, unsigned library_selected,
           const tube5::SponsorSkip &sponsor_skip)
{
    canvas.clear(ink);
    canvas.rectangle(80, 65, 16, 98, accent);
    canvas.text(120, 65, "TUBE5", 11, Color::white);
    canvas.text(120, 185, "TU PANTALLA. TU RITMO.", 4, muted);
    canvas.text(1380, 80, "01.000.001", 3, muted);
    if (build_label[0])
        canvas.text(120, 240, build_label.data(), 2, Color::yellow);
#ifdef TUBE5_DIAGNOSTICS
    if ((nav.screen == tube5::Screen::home || nav.screen == tube5::Screen::library) &&
        !media.opening() && !catalog.loading() && !error[0])
    {
        char pools[192]{};
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
        tube5::native_refstruct_buffer_status(pools, sizeof(pools));
        canvas.text(800, 35, pools, 2, muted);
#endif
#ifdef TUBE5_SURFACE_BUFFER_WRAP
        tube5::native_surface_buffer_status(pools, sizeof(pools));
        canvas.text(800, 65, pools, 2, muted);
#endif
#ifdef TUBE5_MEDIA_BUFFER_WRAP
        tube5::native_media_buffer_status(pools, sizeof(pools));
        canvas.text(800, 95, pools, 2, muted);
#endif
    }
#endif
    if (nav.screen == tube5::Screen::home)
    {
        canvas.text(120, 310, "VAMOS A PROBARLO", 6, Color::white);
        const char *titles[] = {network_test ? "BIBLIOTECA LAN" : "VIDEO DE PRUEBA", "AJUSTES",
                                "ACERCA DE"};
        const char *details[] = {network_test ? "HTTP DESDE TU PC" : "IMAGEN Y SONIDO",
                                 "PRUEBA CON DUALSENSE", "PROYECTO Y CREDITOS"};
        for (unsigned i = 0; i < 3; ++i)
        {
            const unsigned x = 120 + i * 570;
            canvas.rectangle(x, 440, 530, 350,
                             nav.selected == static_cast<int>(i) ? accent : panel);
            canvas.rectangle(x + 8, 448, 514, 334, panel);
            if (i == 0)
                canvas.triangle(x + 265, 490, 65, 120, accent);
            else if (i == 1)
                for (unsigned row = 0; row < 3; ++row)
                {
                    canvas.rectangle(x + 170, 510 + row * 38, 190, 8, muted);
                    canvas.rectangle(x + 210 + row * 32, 500 + row * 38, 18, 28, accent);
                }
            else
                canvas.text(x + 240, 490, "5", 16, accent);
            canvas.text(x + 30, 650, titles[i], 4, Color::white);
            canvas.text(x + 30, 715, details[i], 3, muted);
        }
        canvas.text(120, 830,
                    network_test ? "VIDEO HTTP. PC EN LA MISMA RED."
                                 : "CLIP LOCAL DE 20 SEGUNDOS. NO REQUIERE INTERNET.",
                    3, muted);
    }
    else if (nav.screen == tube5::Screen::library)
    {
        canvas.text(120, 310, "BIBLIOTECA DEL PC", 5, Color::white);
        if (catalog.loading())
            canvas.text(120, 450, catalog.cancelling() ? "CANCELANDO..." : "CARGANDO CATALOGO...",
                        4, Color::white);
        else
        {
            const auto &data = catalog.data();
            for (unsigned i = 0; i < data.count; ++i)
            {
                canvas.rectangle(120, 395 + i * 50, 1540, 44,
                                 i == library_selected ? accent : panel);
                canvas.text(140, 405 + i * 50, data.entries[i].title.data(), 2, Color::white);
            }
            if (library_selected < data.count)
            {
                const auto &entry = data.entries[library_selected];
                char detail[96]{};
                char quality[32]{};
                if (entry.height)
                    std::snprintf(quality, sizeof(quality), "%uP", entry.height);
                else
                    std::snprintf(quality, sizeof(quality), "RESOLUCION DESCONOCIDA");
                std::snprintf(detail, sizeof(detail), "%u:%02u  %s  %s", entry.seconds / 60,
                              entry.seconds % 60, quality,
                              entry.youtube_id[0] ? "ORIGEN YOUTUBE" : "CLIP DE PRUEBA");
                canvas.text(120, 820, detail, 2, muted);
                if (!error[0])
                    canvas.text(120, 855, sponsor_state_text(entry.sponsor.state), 2, muted);
            }
        }
    }
    else if (nav.screen == tube5::Screen::settings)
    {
        canvas.text(120, 320, "AJUSTES", 7, Color::white);
        canvas.text(120, 460, "MANDO", 4, muted);
        canvas.text(650, 460, connected ? "CONECTADO" : "CONECTA TU DUALSENSE", 4, Color::white);
        canvas.text(120, 560, "SPONSORBLOCK", 4, muted);
        canvas.text(650, 560, sponsor_enabled ? "ON - IZQ/DER CAMBIAR" : "OFF - IZQ/DER CAMBIAR", 3,
                    Color::white);
        canvas.text(120, 605, "DATOS: SPONSOR.AJAY.APP - CC BY-NC-SA 4.0", 2, muted);
        canvas.text(120, 660, "REPRODUCCION", 4, muted);
        canvas.text(650, 660, network_test ? "HTTP - X CAMBIAR" : "LOCAL - X CAMBIAR", 4,
                    Color::white);
        canvas.text(120, 765,
                    network_url[0] ? network_url.data() : "HTTP: CONFIGURA NETWORK-TEST-URL.TXT", 2,
                    muted);
    }
    else if (nav.screen == tube5::Screen::about)
    {
        canvas.text(120, 320, "ACERCA DE TUBE5", 7, Color::white);
        canvas.text(120, 455, "CLIENTE MULTIMEDIA HOMEBREW PARA PS5", 4, Color::white);
        canvas.text(120, 545, "GITHUB: MRDEREKIB/TUBE5-PS5", 4, muted);
        canvas.text(120, 635, "BASE: BLACKBEARRELOADED - SDL2 - FFMPEG", 3, muted);
        canvas.text(120, 700, "SDL KITCHENSINK - TUBE5: GPL-3.0-OR-LATER", 3, muted);
        canvas.text(120, 765, "SIN GARANTIA. FUENTES Y LICENCIA EN GITHUB.", 3, muted);
        canvas.text(120, 820, "SPONSORBLOCK: SPONSOR.AJAY.APP - DATOS CC BY-NC-SA 4.0", 2, muted);
    }
    else
    {
        canvas.rectangle(350, 260, 1220, 620, static_cast<Color>(0xff000000));
        if (media.opening())
        {
            const unsigned dots = (SDL_GetTicks() / 350) % 4;
            char loading[64]{};
            std::snprintf(loading, sizeof(loading), "%s%.*s",
                          media.cancelling() ? "CANCELANDO" : "ABRIENDO VIDEO HTTP",
                          static_cast<int>(dots), "...");
            canvas.text(450, 500, loading, 5, Color::white);
            canvas.text(450, 620, "CIRCULO CANCELAR", 3, muted);
            return;
        }
        char status[96]{};
        std::snprintf(
            status, sizeof(status), "%s  %02d:%02d / %02d:%02d",
            media.paused() ? "EN PAUSA" : "REPRODUCIENDO", static_cast<int>(media.position()) / 60,
            static_cast<int>(media.position()) % 60, static_cast<int>(media.duration()) / 60,
            static_cast<int>(media.duration()) % 60);
        canvas.text(350, 900, status, 4, Color::white);
        // Below the video overlay (which ends at y=880), above the clock and
        // diagnostic/error rows. Uses the same clock as the existing timestamp.
        canvas.rectangle(350, 885, 1220, 8, panel);
        const unsigned progress =
            tube5::playback_progress_width(media.position(), media.duration(), 1220);
        canvas.rectangle(350, 885, progress, 8, media.paused() ? Color::yellow : accent);
        if (!error[0] && !selection.active())
        {
            char status[128]{};
            std::snprintf(status, sizeof(status), "SB %s  %u SEG  %u SALTOS  OPTIONS CAMBIAR",
                          sponsor_enabled ? "ON" : "OFF", sponsor_skip.data().count,
                          sponsor_skip.skipped());
            canvas.text(350, 930, status, 2, muted);
        }
        if (selection.active())
        {
            const unsigned target =
                tube5::playback_progress_width(selection.target(), media.duration(), 1220);
            canvas.rectangle(350 + (target > 1216 ? 1216 : target), 882, 4, 14, Color::white);
            if (!error[0])
            {
                char destination[64]{};
                const int seconds = static_cast<int>(selection.target());
                std::snprintf(destination, sizeof(destination), "BUSCAR %02d:%02d", seconds / 60,
                              seconds % 60);
                canvas.text(350, 930, destination, 2, Color::white);
            }
        }
#ifdef TUBE5_DIAGNOSTICS
        char diagnostics[192]{};
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
        tube5::native_refstruct_buffer_status(diagnostics, sizeof(diagnostics));
        canvas.text(800, 35, diagnostics, 2, muted);
#endif
#ifdef TUBE5_SURFACE_BUFFER_WRAP
        tube5::native_surface_buffer_status(diagnostics, sizeof(diagnostics));
        canvas.text(800, 65, diagnostics, 2, muted);
#endif
#ifdef TUBE5_MEDIA_BUFFER_WRAP
        tube5::native_media_buffer_status(diagnostics, sizeof(diagnostics));
        canvas.text(800, 95, diagnostics, 2, muted);
#endif
        canvas.text(700, 125, frame_timing.data(), 2, muted);
        tube5::video_sync_diagnostic_status(diagnostics, sizeof(diagnostics));
        canvas.text(350, 980, diagnostics, 2, muted);
        tube5::video_conversion_status(diagnostics, sizeof(diagnostics));
        canvas.text(350, 1005, diagnostics, 2, muted);
        media.diagnostic_status(diagnostics, sizeof(diagnostics));
        canvas.text(350, 950, diagnostics, 2, muted);
        for (unsigned row = 0; row < 3; ++row)
        {
            tube5::pipeline_diagnostic_status(row, diagnostics, sizeof(diagnostics));
            canvas.text(800, 185 + row * 30, diagnostics, 2, muted);
        }
        tube5::memory_diagnostic_status(diagnostics, sizeof(diagnostics));
        canvas.text(800, 155, diagnostics, 2, muted);
#endif
    }
    if (error[0])
    {
        std::array<char, 65> line{};
        for (std::size_t i = 0; error[i] && i < line.size() - 1; ++i)
            line[i] = error[i] >= 'a' && error[i] <= 'z' ? static_cast<char>(error[i] - 'a' + 'A')
                                                         : error[i];
        // Keep runtime errors below the video overlay, not underneath it.
        const bool playing_screen = nav.screen == tube5::Screen::player;
        canvas.text(playing_screen ? 350 : 120, playing_screen ? 930 : 865, line.data(),
                    playing_screen ? 2 : 3, Color::yellow);
#ifdef TUBE5_DIAGNOSTICS
        if (!playing_screen && !media.opening())
        {
            // Keep the allocation and demux failure readable after teardown.
            // Retrying a fatal read ENOMEM no longer resets these counters.
            char failure[192]{};
#ifdef TUBE5_REFSTRUCT_BUFFER_WRAP
            tube5::native_refstruct_buffer_status(failure, sizeof(failure));
            canvas.text(800, 65, failure, 2, Color::yellow);
#endif
#ifdef TUBE5_SURFACE_BUFFER_WRAP
            tube5::native_surface_buffer_status(failure, sizeof(failure));
            canvas.text(800, 95, failure, 2, Color::yellow);
#endif
#ifdef TUBE5_MEDIA_BUFFER_WRAP
            tube5::native_media_buffer_status(failure, sizeof(failure));
            canvas.text(800, 890, failure, 2, Color::yellow);
#endif
            tube5::memory_diagnostic_status(failure, sizeof(failure));
            canvas.text(120, 915, failure, 2, Color::yellow);
            tube5::memory_failure_stage_status(failure, sizeof(failure));
            canvas.text(800, 915, failure, 2, Color::yellow);
            tube5::flexible_memory_status(failure, sizeof(failure));
            canvas.text(120, 975, failure, 2, Color::yellow);
            tube5::pipeline_diagnostic_status(0, failure, sizeof(failure));
            canvas.text(120, 945, failure, 2, Color::yellow);
            tube5::memory_allocation_origin_status(0, failure, sizeof(failure));
            canvas.text(800, 945, failure, 2, Color::yellow);
            tube5::memory_allocation_origin_status(1, failure, sizeof(failure));
            canvas.text(800, 975, failure, 2, Color::yellow);
        }
#endif
    }
    canvas.text(120, nav.screen == tube5::Screen::player ? 1040 : 995,
                nav.screen == tube5::Screen::player
                    ? (selection.active()
                           ? "IZQ/DER 5 SEG  L1/R1 30 SEG  X CONFIRMAR  CIRCULO CANCELAR"
                           : "X PAUSA  IZQ/DER 5 SEG  TRIANGULO BUSCAR  CIRCULO VOLVER")
                    : (nav.screen == tube5::Screen::library
                           ? "CRUCETA MOVER  X REPRODUCIR  CIRCULO VOLVER  OPTIONS RECARGAR"
                           : "CRUCETA MOVER   X ABRIR   CIRCULO VOLVER   OPTIONS AJUSTES"),
                3, muted);
}

[[noreturn]] void fatal(const char *stage)
{
#if defined(TUBE5_DIAGNOSTICS) && !defined(TUBE5_HOST)
    tube5_boot_checkpoint("Tube5 ERROR: etapa siguiente");
    tube5_boot_checkpoint(stage);
    tube5_boot_checkpoint(SDL_GetError());
    for (;;)
        (void)sceKernelUsleep(1000000);
#else
    std::fprintf(stderr, "Tube5 %s: %s\n", stage, SDL_GetError());
#ifdef TUBE5_HOST
    std::exit(1);
#else
    for (;;)
        SDL_Delay(1000);
#endif
#endif
}
} // namespace

int main()
{
    tube5_boot_checkpoint("Tube5 D03: MAIN - leer etiqueta");
    ps5::demo::read_asset_text("/app0/build-label.txt", std::span{build_label}, "");
    for (char &character : build_label)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
#ifdef TUBE5_HOST
    ps5::demo::read_asset_text(".local/network-test-url.txt", std::span{network_url}, "");
#else
    ps5::demo::read_asset_text("/app0/network-test-url.txt", std::span{network_url}, "");
#endif
#ifndef TUBE5_HOST
    tube5_boot_checkpoint("Tube5 D04: iniciar UserService");
    (void)sceUserServiceInitialize(nullptr);
#endif
    if (tube5::prepare_native_sdl() != 0)
    {
        tube5_boot_checkpoint("Tube5 ERROR: modulo Keyboard no disponible - SDL no iniciado");
#ifndef TUBE5_HOST
        for (;;)
            (void)sceKernelUsleep(1000000);
#else
        return 1;
#endif
    }
    tube5_boot_checkpoint("Tube5 D05: iniciar SDL API");
    SDL_SetMainReady();
    tube5_boot_checkpoint("Tube5 D05A: SDL API lista - iniciar eventos/video");
#if defined(TUBE5_HOST) && !defined(TUBE5_OFFSCREEN)
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        fatal("SDL VIDEO");
#else
    // SDL's PS5 window framebuffer callback does not supply its format/pixels/
    // pitch outputs. Use SDL only as an offscreen compositor; native VideoOut
    // owns the display, so no second SDL VideoOut handle is opened.
    if (SDL_Init(SDL_INIT_EVENTS) != 0)
        fatal("SDL EVENTS");
#endif
    tube5_boot_checkpoint("Tube5 D06: eventos/video OK - iniciar AUDIO");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        fatal("SDL AUDIO");
    tube5_boot_checkpoint("Tube5 D07: AUDIO OK - iniciar MANDO");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fatal("SDL MANDO");
    SDL_Window *window = nullptr;
    SDL_Surface *output = nullptr;
    SDL_Renderer *renderer = nullptr;
#ifndef TUBE5_HOST
    tube5::NativeFramebuffer native_pixels;
#endif
#if defined(TUBE5_HOST) && !defined(TUBE5_OFFSCREEN)
    tube5_boot_checkpoint("Tube5 D08: MANDO OK - crear ventana");
    window = SDL_CreateWindow("Tube5", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
                              SDL_WINDOW_SHOWN);
    if (!window)
        fatal("WINDOW");
    tube5_boot_checkpoint("Tube5 D09: ventana OK - crear renderer");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#else
    tube5_boot_checkpoint("Tube5 D08: MANDO OK - superficie RGBA");
#ifdef TUBE5_HOST
    // Exercise SDL's externally-owned surface path on the host too.
    auto host_pixels = std::unique_ptr<std::uint32_t[]>{new std::uint32_t[1920 * 1080]};
    output = SDL_CreateRGBSurfaceWithFormatFrom(host_pixels.get(), 1920, 1080, 32, 1920 * 4,
                                                SDL_PIXELFORMAT_RGBA32);
#else
    const int allocation = native_pixels.allocate(1920 * 1080 * 4);
    if (allocation != 0)
    {
        tube5_boot_result("Tube5 D08: memoria directa error",
                          static_cast<std::uint32_t>(allocation));
        (void)SDL_SetError("unable to allocate direct-memory UI framebuffer");
        fatal("DIRECT UI MEMORY");
    }
    // SDL owns only the surface metadata, not this external pixel buffer.
    output = SDL_CreateRGBSurfaceWithFormatFrom(native_pixels.data(), 1920, 1080, 32, 1920 * 4,
                                                SDL_PIXELFORMAT_RGBA32);
#endif
    if (!output)
        fatal("OUTPUT SURFACE");
    tube5_boot_checkpoint("Tube5 D09: superficie OK - renderer directo");
    renderer = SDL_CreateSoftwareRenderer(output);
#endif
    if (!renderer)
        fatal("RENDERER");
    tube5_boot_checkpoint("Tube5 D10: renderer OK - framebuffer");
    if (SDL_RenderSetLogicalSize(renderer, 1920, 1080) != 0)
        fatal("LOGICAL SIZE");
    SDL_Texture *ui = nullptr;
    std::unique_ptr<std::uint32_t[]> pixels;
    std::uint32_t *canvas_pixels = nullptr;
    if (output)
    {
        // The offscreen software renderer already draws into this surface.
        // Paint the UI in place: no extra 1080p texture or upload buffer.
        if (!output->pixels || !output->format || output->w != 1920 || output->h != 1080 ||
            output->pitch != 1920 * 4 || output->format->format != SDL_PIXELFORMAT_RGBA32)
        {
            (void)SDL_SetError("unexpected offscreen surface layout");
            fatal("OUTPUT LAYOUT");
        }
        canvas_pixels = static_cast<std::uint32_t *>(output->pixels);
    }
    else
    {
        ui = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 1920,
                               1080);
        if (!ui)
            fatal("UI TEXTURE");
        pixels.reset(new std::uint32_t[1920 * 1080]);
        canvas_pixels = pixels.get();
    }
    Canvas canvas{canvas_pixels, true};
    SDL_GameController *controller = nullptr;
    const auto connect = [&]()
    {
        if (!controller)
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_IsGameController(i))
                {
                    controller = SDL_GameControllerOpen(i);
                    if (controller)
                        break;
                }
    };
    connect();
    tube5_boot_checkpoint("Tube5 D11: UI OK - iniciar reproductor");
    tube5::Navigation nav;
    tube5::MediaPlayer media;
    tube5::SeekSelection selection;
    tube5::LanCatalog catalog;
    tube5::SponsorSkip sponsor_skip;
    unsigned library_selected = 0;
    bool library_playback = false;
    std::array<char, 256> active_media_url{};
    char error[256]{};
    const bool kit_ready = Kit_Init(KIT_INIT_NETWORK) == 0;
    if (!kit_ready)
        std::snprintf(error, sizeof(error), "PLAYER INIT: %s", Kit_GetError());
    tube5_boot_checkpoint("Tube5 D12: iniciar bucle de eventos");
    Uint32 select_ready_at = 0;
    bool opening_player = false;
    const auto start_player = [&](const char *source)
    {
        selection.cancel();
        sponsor_skip.reset(library_playback && library_selected < catalog.data().count
                               ? catalog.data().entries[library_selected].sponsor
                               : tube5::SponsorData{});
        opening_player = true;
        error[0] = '\0';
#ifdef TUBE5_HOST
        constexpr const char *clip = "assets/test-clip.mp4";
#else
        constexpr const char *clip = "/app0/assets/test-clip.mp4";
#endif
        const bool configured =
            !network_test || (source && std::string_view{source}.starts_with("http://"));
        if (!kit_ready || !configured ||
            !(network_test ? media.begin_open(source) : media.open(renderer, clip)))
        {
            std::snprintf(error, sizeof(error), "%s",
                          !kit_ready    ? "PLAYER INIT FAILED"
                          : !configured ? "HTTP: CONFIGURA NETWORK-TEST-URL.TXT"
                                        : media.error());
            nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
        }
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
        SDL_FlushEvent(SDL_KEYDOWN);
        select_ready_at = SDL_GetTicks() + 350;
    };
    const auto load_catalog = [&]()
    {
        error[0] = 0;
        library_selected = 0;
        if (!kit_ready || !catalog.begin(network_url.data()))
            std::snprintf(error, sizeof(error), "%s",
                          kit_ready ? catalog.error() : "PLAYER INIT FAILED");
    };
    const auto command = [&](tube5::Command action)
    {
        if (nav.screen == tube5::Screen::home && nav.selected == 0 && network_test &&
            action == tube5::Command::select)
        {
            nav.screen = tube5::Screen::library;
            error[0] = 0;
            if (!catalog.data().count)
                load_catalog();
            return;
        }
        if (nav.screen == tube5::Screen::library)
        {
            if (catalog.loading())
            {
                if (action == tube5::Command::back)
                    catalog.cancel();
                return;
            }
            const unsigned count = catalog.data().count;
            if (count && action == tube5::Command::previous)
                library_selected = (library_selected + count - 1) % count;
            else if (count && action == tube5::Command::next)
                library_selected = (library_selected + 1) % count;
            else if (action == tube5::Command::settings)
                load_catalog();
            else if (action == tube5::Command::back)
            {
                nav.screen = tube5::Screen::home;
                error[0] = 0;
            }
            else if (action == tube5::Command::select &&
                     catalog.media_url(library_selected, active_media_url.data(),
                                       active_media_url.size()))
            {
                library_playback = true;
                nav.screen = tube5::Screen::player;
                start_player(active_media_url.data());
            }
            return;
        }
        if (nav.screen == tube5::Screen::settings &&
            (action == tube5::Command::previous || action == tube5::Command::next))
        {
            sponsor_enabled = !sponsor_enabled;
            return;
        }
        if (nav.screen == tube5::Screen::settings && action == tube5::Command::select)
        {
            network_test = !network_test;
            return;
        }
        if (nav.screen == tube5::Screen::player)
        {
            if (action == tube5::Command::settings)
            {
                sponsor_enabled = !sponsor_enabled;
                return;
            }
            if (media.opening())
            {
                if (action == tube5::Command::back)
                    media.cancel_open();
                return;
            }
            if (action == tube5::Command::search)
            {
                if (selection.active())
                    selection.cancel();
                else
                    selection.begin(media.position(), media.duration());
                return;
            }
            if (selection.active())
            {
                if (action == tube5::Command::previous || action == tube5::Command::next)
                    selection.move(action == tube5::Command::previous ? -5.0 : 5.0);
                else if (action == tube5::Command::coarse_previous ||
                         action == tube5::Command::coarse_next)
                    selection.move(action == tube5::Command::coarse_previous ? -30.0 : 30.0);
                else if (action == tube5::Command::back)
                    selection.cancel();
                else if (action == tube5::Command::select &&
                         static_cast<Sint32>(SDL_GetTicks() - select_ready_at) >= 0)
                {
                    if (const auto destination = selection.confirm(media.duration()))
                    {
                        sponsor_skip.manual_seek();
                        media.seek_to(*destination);
                    }
                    // A queued second X must not immediately toggle pause.
                    select_ready_at = SDL_GetTicks() + 350;
                }
                return;
            }
            if (action == tube5::Command::select)
            {
                if (static_cast<Sint32>(SDL_GetTicks() - select_ready_at) >= 0)
                    media.toggle_pause();
            }
            else if (action == tube5::Command::previous || action == tube5::Command::next)
            {
                sponsor_skip.manual_seek();
                media.seek(action == tube5::Command::previous ? -5.0 : 5.0);
            }
            else if (action == tube5::Command::back)
            {
                selection.cancel();
                media.close();
                nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
            }
            return;
        }
        if (nav.apply(action))
        {
            library_playback = false;
            start_player(network_url.data());
        }
    };
#ifndef TUBE5_HOST
    (void)sceSystemServiceHideSplashScreen();
#endif
#ifdef TUBE5_HOST
    if (const char *url = std::getenv("TUBE5_PREVIEW_LIBRARY_URL"))
    {
        nav.screen = tube5::Screen::library;
        (void)catalog.begin(url);
    }
    if (const char *screen = std::getenv("TUBE5_PREVIEW_SCREEN"))
    {
        if (std::string_view{screen} == "settings")
            nav.screen = tube5::Screen::settings;
        else if (std::string_view{screen} == "about")
            nav.screen = tube5::Screen::about;
    }
    bool running = true;
#endif
    auto render_frame = [&](Canvas *destination) noexcept
    {
        const Uint32 frame_start = SDL_GetTicks();
        static Uint32 previous_frame_start = frame_start;
        const Uint32 interval = frame_start - previous_frame_start;
        previous_frame_start = frame_start;
        opening_player = false;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_CONTROLLERDEVICEADDED)
                connect();
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller &&
                     event.cdevice.which ==
                         SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)))
            {
                SDL_GameControllerClose(controller);
                controller = nullptr;
                connect();
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    command(tube5::Command::previous);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    command(tube5::Command::next);
                    break;
                case SDL_CONTROLLER_BUTTON_A:
                    command(tube5::Command::select);
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    command(tube5::Command::back);
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    command(tube5::Command::settings);
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    command(tube5::Command::search);
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    command(tube5::Command::coarse_previous);
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    command(tube5::Command::coarse_next);
                    break;
                default:
                    break;
                }
            }
            else if (event.type == SDL_KEYDOWN && !event.key.repeat)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_LEFT:
                case SDLK_UP:
                    command(tube5::Command::previous);
                    break;
                case SDLK_RIGHT:
                case SDLK_DOWN:
                    command(tube5::Command::next);
                    break;
                case SDLK_RETURN:
                case SDLK_SPACE:
                    command(tube5::Command::select);
                    break;
                case SDLK_ESCAPE:
                    command(tube5::Command::back);
                    break;
                case SDLK_TAB:
                    command(tube5::Command::settings);
                    break;
                case SDLK_s:
                    command(tube5::Command::search);
                    break;
                case SDLK_PAGEUP:
                    command(tube5::Command::coarse_previous);
                    break;
                case SDLK_PAGEDOWN:
                    command(tube5::Command::coarse_next);
                    break;
                default:
                    break;
                }
            }
#ifdef TUBE5_HOST
            else if (event.type == SDL_QUIT)
                running = false;
#endif
            if (opening_player)
                break;
        }
        if (media.opening())
        {
            const auto result = media.poll_open(renderer);
            if (result == tube5::MediaPlayer::OpenResult::failed)
            {
                std::snprintf(error, sizeof(error), "%s", media.error());
                nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
            }
            else if (result == tube5::MediaPlayer::OpenResult::cancelled)
                nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
            else if (result == tube5::MediaPlayer::OpenResult::ready)
            {
                SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
                SDL_FlushEvent(SDL_KEYDOWN);
                select_ready_at = SDL_GetTicks() + 350;
            }
        }
        if (nav.screen == tube5::Screen::player && media.check_network_failure())
        {
            std::snprintf(error, sizeof(error), "%s", media.error());
            nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
        }
        if (nav.screen == tube5::Screen::player && media.finished())
        {
            media.close();
            nav.screen = library_playback ? tube5::Screen::library : tube5::Screen::home;
        }
        if (nav.screen == tube5::Screen::player && !media.opening())
            if (const auto target = sponsor_skip.target(
                    media.position(), media.duration(), sponsor_enabled, media.paused(),
                    media.seeking() || selection.active() || media.frames_uploaded() == 0))
                media.seek_to(*target);
        if (nav.screen != tube5::Screen::player)
            selection.cancel();
        if (catalog.loading())
        {
            catalog.poll();
            if (!catalog.loading())
            {
                if (catalog.cancelling())
                    nav.screen = tube5::Screen::home;
                else if (catalog.error()[0])
                    std::snprintf(error, sizeof(error), "%s", catalog.error());
                SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
                SDL_FlushEvent(SDL_KEYDOWN);
            }
        }
        if (output && SDL_LockSurface(output) != 0)
            fatal("UI SURFACE LOCK");
        const Uint32 paint_start = SDL_GetTicks();
        paint(canvas, nav, media, controller != nullptr, error, selection, catalog,
              library_selected, sponsor_skip);
        if (output)
            SDL_UnlockSurface(output);
        else
        {
            if (SDL_UpdateTexture(ui, nullptr, pixels.get(), 1920 * 4) != 0 ||
                SDL_RenderCopy(renderer, ui, nullptr, nullptr) != 0)
                fatal("UI UPLOAD");
        }
        const Uint32 video_start = SDL_GetTicks();
        if (nav.screen == tube5::Screen::player)
        {
            media.draw(renderer, SDL_Rect{350, 260, 1220, 620});
            if (!media.opening() && media.error()[0])
                std::snprintf(error, sizeof(error), "%s", media.error());
        }
        const Uint32 video_end = SDL_GetTicks();
        SDL_RenderPresent(renderer);
#ifdef TUBE5_HOST
        static bool first_frame = true;
        if (first_frame)
        {
            tube5_boot_checkpoint("Tube5 D13: primer frame presentado");
            first_frame = false;
        }
        if (const char *preview = std::getenv("TUBE5_PREVIEW"); preview && !catalog.loading())
        {
            SDL_Surface *capture =
                SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_RGBA32);
            if (capture && SDL_RenderReadPixels(renderer, nullptr, capture->format->format,
                                                capture->pixels, capture->pitch) == 0)
                (void)SDL_SaveBMP(capture, preview);
            SDL_FreeSurface(capture);
            running = false;
        }
#endif
        const Uint32 blit_start = SDL_GetTicks();
        if (destination && output)
        {
            const unsigned pitch = static_cast<unsigned>(output->pitch / 4);
            const std::span<const std::uint32_t> frame{
                static_cast<const std::uint32_t *>(output->pixels),
                static_cast<std::size_t>(pitch) * output->h};
            if (!destination->blit_linear(frame, pitch))
            {
                (void)SDL_SetError("invalid native frame dimensions");
                fatal("NATIVE BLIT");
            }
        }
#ifdef TUBE5_DIAGNOSTICS
        std::snprintf(frame_timing.data(), frame_timing.size(),
                      "MS LOOP %u WORK %u UI %u VIDEO %u COPY %u", interval,
                      SDL_GetTicks() - frame_start, video_start - paint_start,
                      video_end - video_start, SDL_GetTicks() - blit_start);
#else
        (void)interval;
        (void)paint_start;
        (void)video_start;
        (void)video_end;
        (void)blit_start;
#endif
#ifdef TUBE5_HOST
        // The host has no native presentation wait. On PS5 run_frames already
        // waits for VideoOut vblank after drawing/flushing/submitting. Sleeping
        // here as well adds to that wait and can cap a 30fps clip near 20fps.
        const Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 33)
            SDL_Delay(33 - elapsed);
#endif
    };
#ifdef TUBE5_HOST
    while (running)
        render_frame(nullptr);
#else
    tube5_boot_checkpoint("Tube5 D12V: iniciar presentacion VideoOut nativa");
    using Frame = decltype(render_frame);
    ps5::demo::run_frames([](Canvas &destination, void *state) noexcept
                          { (*static_cast<Frame *>(state))(&destination); }, &render_frame,
                          "Tube5 D13: primer frame nativo presentado");
#endif
    media.close();
    catalog.cancel();
    while (catalog.loading())
    {
        catalog.poll();
        SDL_Delay(1);
    }
    if (controller)
        SDL_GameControllerClose(controller);
    if (kit_ready)
        Kit_Quit();
    SDL_DestroyTexture(ui);
    SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    SDL_FreeSurface(output);
    SDL_Quit();
    return 0;
}
