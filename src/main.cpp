// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "boot_diagnostics.hpp"
#include "demo_renderer.hpp"
#include "media_player.hpp"
#include "navigation.hpp"
#include "native_sdl.hpp"
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

void paint(Canvas &canvas, const tube5::Navigation &nav, tube5::MediaPlayer &media, bool connected,
           const char *error)
{
    canvas.clear(ink);
    canvas.rectangle(80, 65, 16, 98, accent);
    canvas.text(120, 65, "TUBE5", 11, Color::white);
    canvas.text(120, 185, "TU PANTALLA. TU RITMO.", 4, muted);
    canvas.text(1380, 80, "01.000.001", 3, muted);
    if (build_label[0])
        canvas.text(120, 240, build_label.data(), 2, Color::yellow);
    if (nav.screen == tube5::Screen::home)
    {
        canvas.text(120, 310, "VAMOS A PROBARLO", 6, Color::white);
        const char *titles[] = {"VIDEO DE PRUEBA", "AJUSTES", "ACERCA DE"};
        const char *details[] = {"IMAGEN Y SONIDO", "PRUEBA CON DUALSENSE", "PROYECTO Y CREDITOS"};
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
        canvas.text(120, 830, "CLIP LOCAL DE 20 SEGUNDOS. NO REQUIERE INTERNET.", 3, muted);
    }
    else if (nav.screen == tube5::Screen::settings)
    {
        canvas.text(120, 320, "AJUSTES", 7, Color::white);
        canvas.text(120, 460, "MANDO", 4, muted);
        canvas.text(650, 460, connected ? "CONECTADO" : "CONECTA TU DUALSENSE", 4, Color::white);
        canvas.text(120, 560, "SPONSORBLOCK", 4, muted);
        canvas.text(650, 560, "PROXIMAMENTE", 4, Color::white);
        canvas.text(120, 660, "REPRODUCCION", 4, muted);
        canvas.text(650, 660, "VIDEO LOCAL CON AUDIO", 4, Color::white);
    }
    else if (nav.screen == tube5::Screen::about)
    {
        canvas.text(120, 320, "ACERCA DE TUBE5", 7, Color::white);
        canvas.text(120, 455, "CLIENTE MULTIMEDIA HOMEBREW PARA PS5", 4, Color::white);
        canvas.text(120, 545, "GITHUB: MRDEREKIB/TUBE5-PS5", 4, muted);
        canvas.text(120, 635, "BASE: BLACKBEARRELOADED - SDL2 - FFMPEG", 3, muted);
        canvas.text(120, 700, "SDL KITCHENSINK - TUBE5: GPL-3.0-OR-LATER", 3, muted);
        canvas.text(120, 765, "SIN GARANTIA. FUENTES Y LICENCIA EN GITHUB.", 3, muted);
    }
    else
    {
        canvas.rectangle(350, 260, 1220, 620, static_cast<Color>(0xff000000));
        char status[96]{};
        std::snprintf(
            status, sizeof(status), "%s  %02d:%02d / %02d:%02d",
            media.paused() ? "EN PAUSA" : "REPRODUCIENDO", static_cast<int>(media.position()) / 60,
            static_cast<int>(media.position()) % 60, static_cast<int>(media.duration()) / 60,
            static_cast<int>(media.duration()) % 60);
        canvas.text(350, 900, status, 4, Color::white);
    }
    if (error[0])
    {
        std::array<char, 65> line{};
        for (std::size_t i = 0; error[i] && i < line.size() - 1; ++i)
            line[i] = error[i] >= 'a' && error[i] <= 'z' ? static_cast<char>(error[i] - 'a' + 'A')
                                                         : error[i];
        canvas.text(120, 865, line.data(), 3, Color::yellow);
    }
    canvas.text(120, 995,
                nav.screen == tube5::Screen::player
                    ? "X PAUSA   IZQ/DER 5 SEG   CIRCULO VOLVER"
                    : "CRUCETA MOVER   X ABRIR   CIRCULO VOLVER   OPTIONS AJUSTES",
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
    output = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_RGBA32);
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
    char error[256]{};
    const bool kit_ready = Kit_Init(0) == 0;
    if (!kit_ready)
        std::snprintf(error, sizeof(error), "PLAYER INIT: %s", Kit_GetError());
    tube5_boot_checkpoint("Tube5 D12: iniciar bucle de eventos");
    const auto command = [&](tube5::Command action)
    {
        if (nav.screen == tube5::Screen::player)
        {
            if (action == tube5::Command::select)
                media.toggle_pause();
            else if (action == tube5::Command::previous || action == tube5::Command::next)
                media.seek(action == tube5::Command::previous ? -5.0 : 5.0);
            else if (action == tube5::Command::back)
            {
                media.close();
                nav.apply(action);
            }
            return;
        }
        if (nav.apply(action))
        {
            error[0] = '\0';
#ifdef TUBE5_HOST
            constexpr const char *clip = "assets/test-clip.mp4";
#else
            constexpr const char *clip = "/app0/assets/test-clip.mp4";
#endif
            if (!kit_ready || !media.open(renderer, clip))
            {
                std::snprintf(error, sizeof(error), "%s",
                              kit_ready ? media.error() : "PLAYER INIT FAILED");
                std::fprintf(stderr, "Tube5: %s\n", error);
                nav.screen = tube5::Screen::home;
            }
        }
    };
#ifndef TUBE5_HOST
    (void)sceSystemServiceHideSplashScreen();
#endif
#ifdef TUBE5_HOST
    bool running = true;
#endif
    auto render_frame = [&](Canvas *destination) noexcept
    {
        const Uint32 frame_start = SDL_GetTicks();
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
                default:
                    break;
                }
            }
#ifdef TUBE5_HOST
            else if (event.type == SDL_QUIT)
                running = false;
#endif
        }
        if (nav.screen == tube5::Screen::player && media.finished())
        {
            media.close();
            nav.screen = tube5::Screen::home;
        }
        if (output && SDL_LockSurface(output) != 0)
            fatal("UI SURFACE LOCK");
        paint(canvas, nav, media, controller != nullptr, error);
        if (output)
            SDL_UnlockSurface(output);
        else
        {
            if (SDL_UpdateTexture(ui, nullptr, pixels.get(), 1920 * 4) != 0 ||
                SDL_RenderCopy(renderer, ui, nullptr, nullptr) != 0)
                fatal("UI UPLOAD");
        }
        if (nav.screen == tube5::Screen::player)
            media.draw(renderer, SDL_Rect{350, 260, 1220, 620});
        SDL_RenderPresent(renderer);
#ifdef TUBE5_HOST
        static bool first_frame = true;
        if (first_frame)
        {
            tube5_boot_checkpoint("Tube5 D13: primer frame presentado");
            first_frame = false;
        }
        if (const char *preview = std::getenv("TUBE5_PREVIEW"))
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
        const Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < 33)
            SDL_Delay(33 - elapsed);
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
