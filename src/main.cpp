// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "boot_diagnostics.hpp"
#include "demo_renderer.hpp"
#include "media_player.hpp"
#include "navigation.hpp"
#include "native_sdl.hpp"
#include "pipeline_diagnostics.hpp"
#include "memory_diagnostics.hpp"
#include "native_parser_buffers.hpp"
#include "native_media_buffers.hpp"
#include "native_surface_buffers.hpp"
#include "native_refstruct_buffers.hpp"
#include "native_framebuffer.hpp"
#include "playback_progress.hpp"
#include "seek_selection.hpp"
#include "lan_catalog.hpp"
#include "preferences.hpp"
#include "failure_log.hpp"
#include "hardware_video_probe.hpp"
#include "hardware_frame_probe.hpp"
#include "nv12_rgba.hpp"
#include "sponsor_notice.hpp"
#include "youtube_search.hpp"
#include "youtube_probe.hpp"
#include "video_sync_diagnostics.hpp"
#include "video_conversion.hpp"
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
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
#ifdef TUBE5_HW_PROBE
tube5::HardwareVideoReport hardware_report;
#endif
#ifdef TUBE5_HW_FRAME
struct HardwarePreview
{
    SDL_Texture *texture = nullptr;
};

int capture_hardware_nv12(const tube5::VideoOutput &frame, void *context) noexcept
{
    auto *preview = static_cast<HardwarePreview *>(context);
    if (!preview || !preview->texture || frame.format != 0 || !frame.buffer ||
        frame.width != 1920 || frame.height < 1080 || frame.height > 1088 ||
        frame.pitch_bytes < frame.width || (frame.pitch_bytes & 1u))
        return -EINVAL;
    void *locked = nullptr;
    int rgba_pitch = 0;
    if (SDL_LockTexture(preview->texture, nullptr, &locked, &rgba_pitch) != 0 || !locked ||
        rgba_pitch < 1920 * 4)
    {
        if (locked)
            SDL_UnlockTexture(preview->texture);
        return -ENOMEM;
    }
    const bool converted = tube5::nv12_to_rgba(
        static_cast<const std::uint8_t *>(frame.buffer), frame.bytes, 1920, 1080, frame.height,
        frame.pitch_bytes, static_cast<std::uint8_t *>(locked),
        static_cast<std::size_t>(rgba_pitch));
    SDL_UnlockTexture(preview->texture);
    return converted ? 0 : -EINVAL;
}
#endif
std::array<char, 256> network_url{};
std::array<char, 65> console_token{};
tube5::VideoKeyboard video_keyboard;
std::array<char, 12> direct_video_id{"bHIhgxav9LY"};
unsigned direct_height = 0, quality_selected = 0;
enum class SearchPage
{
    keyboard,
    searching,
    results,
    preparing
};
SearchPage search_page = SearchPage::keyboard;
tube5::SearchKeyboard search_keyboard;
tube5::SearchData search_results;
unsigned search_selected = 0;
bool network_test = true;
unsigned source_selected = 0;
bool sponsor_enabled = true;
bool preference_saved = true;
tube5::SponsorNotice sponsor_notice;
#ifdef TUBE5_HOST
tube5::Preferences preferences(".local/tube5");
#else
tube5::Preferences preferences("/download0", false);
#endif
void toggle_sponsor() noexcept
{
    sponsor_enabled = !sponsor_enabled;
    preference_saved = preferences.save(sponsor_enabled);
}
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
           const tube5::SponsorSkip &sponsor_skip, const tube5::YouTubeProbe &probe)
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
#ifdef TUBE5_PARSER_BUFFER_WRAP
        tube5::native_parser_buffer_status(pools, sizeof(pools));
        canvas.text(800, 125, pools, 2, muted);
#endif
    }
#endif
    if (nav.screen == tube5::Screen::home)
    {
        canvas.text(120, 310, "VAMOS A PROBARLO", 6, Color::white);
        const char *titles[] = {"REPRODUCIR", "AJUSTES", "ACERCA DE"};
        const char *details[] = {"YOUTUBE - HTTP - LOCAL", "PRUEBA CON DUALSENSE",
                                 "PROYECTO Y CREDITOS"};
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
        canvas.text(120, 830, "YOUTUBE DIRECTO DESDE LA CONSOLA. SIN PC.", 3, muted);
    }
    else if (nav.screen == tube5::Screen::sources)
    {
        canvas.text(120, 310, "REPRODUCIR - ELIGE FUENTE", 5, Color::white);
        const char *titles[] = {"YOUTUBE", "HTTP - BIBLIOTECA LAN", "LOCAL - CLIP DE PRUEBA"};
        const char *details[] = {"DIRECTO. SIN PC. VIDEO Y CALIDAD.",
                                 "PRUEBAS CON EL SERVIDOR DEL PC.", "20 SEGUNDOS. SIN CONEXION."};
        for (unsigned i = 0; i < 3; ++i)
        {
            const int y = 420 + static_cast<int>(i) * 130;
            canvas.rectangle(120, y, 1540, 110, source_selected == i ? accent : panel);
            canvas.text(150, y + 15, titles[i], 4, Color::white);
            canvas.text(150, y + 65, details[i], 3, muted);
        }
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
    else if (nav.screen == tube5::Screen::search)
    {
        canvas.text(120, 310, "BUSCAR EN YOUTUBE", 5, Color::white);
        canvas.text(120, 370,
                    search_keyboard.query()[0] ? search_keyboard.query() : "ESCRIBE UNA BUSQUEDA",
                    3, muted);
        if (search_page == SearchPage::keyboard)
        {
            for (unsigned i = 0; i < tube5::SearchKeyboard::keys.size(); ++i)
            {
                const unsigned x = 120 + (i % 10) * 135, y = 460 + (i / 10) * 75;
                canvas.rectangle(x, y, 110, 60, i == search_keyboard.selected() ? accent : panel);
                char key[2]{tube5::SearchKeyboard::keys[i], 0};
                canvas.text(x + (key[0] == ' ' ? 30 : 35), y + 15, key[0] == ' ' ? "ESP" : key,
                            key[0] == ' ' ? 2 : 4, Color::white);
            }
            canvas.text(120, 825, "TRIANGULO BUSCAR   OPTIONS BORRAR   CIRCULO VOLVER", 3, muted);
        }
        else if (search_page == SearchPage::results)
        {
            if (!search_results.count)
                canvas.text(120, 470, "SIN RESULTADOS COMPATIBLES", 4, muted);
            for (unsigned i = 0; i < search_results.count; ++i)
            {
                canvas.rectangle(120, 430 + i * 50, 1540, 44,
                                 i == search_selected ? accent : panel);
                canvas.text(140, 440 + i * 50, search_results.entries[i].title.data(), 2,
                            Color::white);
            }
            if (!error[0] && search_selected < search_results.count)
            {
                const auto &entry = search_results.entries[search_selected];
                char detail[96]{};
                std::snprintf(detail, sizeof(detail), "%s  %u:%02u", entry.channel.data(),
                              entry.seconds / 60, entry.seconds % 60);
                canvas.text(120, 860, detail, 2, muted);
            }
            canvas.text(120, 915, "X PREPARAR EN EL PC   TRIANGULO NUEVA BUSQUEDA", 3, muted);
        }
        else
        {
            canvas.text(120, 500,
                        search_page == SearchPage::searching ? "BUSCANDO..."
                                                             : "PREPARANDO VIDEO EN EL PC...",
                        4, Color::white);
            canvas.text(120, 590, "LA INTERFAZ SIGUE ACTIVA - CIRCULO VOLVER", 3, muted);
            if (search_page == SearchPage::preparing)
                canvas.text(120, 655, "AL VOLVER, EL TRABAJO DEL PC CONTINUA", 2, muted);
        }
    }
    else if (nav.screen == tube5::Screen::video_input)
    {
        canvas.text(120, 310, "ABRIR YOUTUBE - ID O URL", 4, Color::white);
        const std::string_view input = video_keyboard.input();
        canvas.text(120, 380,
                    input.empty()
                        ? "ESCRIBE EL ID DE 11 CARACTERES O EL ENLACE"
                        : tube5::youtube_after(input, input.size() > 100 ? input.size() - 100 : 0),
                    2, Color::white);
        for (unsigned i = 0; i < tube5::VideoKeyboard::keys.size(); ++i)
        {
            const unsigned x = 120 + (i % 9) * 130, y = 435 + (i / 9) * 52;
            canvas.rectangle(x, y, 112, 44, i == video_keyboard.selected() ? accent : panel);
            char key[2]{tube5::VideoKeyboard::keys[i], 0};
            canvas.text(x + 43, y + 10, key[0] == ' ' ? "ESP" : key, key[0] == ' ' ? 2 : 3,
                        Color::white);
        }
        canvas.text(120, 920, "LOS ID DISTINGUEN MAYUSCULAS Y MINUSCULAS - SIN PC", 2, muted);
    }
    else if (nav.screen == tube5::Screen::quality)
    {
        canvas.text(120, 310, "CALIDAD - FORMATOS DEL VIDEO", 4, Color::white);
        canvas.text(120, 365, probe.result().title.data(), 2, muted);
        const auto &formats = probe.result().formats;
        if (!formats.quality_count)
            canvas.text(120, 435, "CONSULTA PRIMERO UN VIDEO", 3, muted);
        const unsigned first = quality_selected > 7 ? quality_selected - 7 : 0;
        for (unsigned i = first; i < formats.quality_count && i < first + 8; ++i)
        {
            const unsigned y = 420 + (i - first) * 51;
            const auto &quality = formats.qualities[i];
            canvas.rectangle(120, y, 1540, 43, i == quality_selected ? accent : panel);
            char label[128]{};
            std::snprintf(label, sizeof(label), "%uP - %s", quality.height,
                          quality.playable    ? "LISTA: MP4 H.264 + AAC"
                          : quality.candidate ? "CONSULTAR: H.264 + AUDIO SEPARADO"
                          : quality.separate  ? "NO COMPATIBLE: CODEC/FPS/URL"
                                              : "NO COMPATIBLE CON LA RUTA ACTUAL");
            canvas.text(145, y + 10, label, 3, Color::white);
        }
        canvas.text(120, 925, "AUTO: MAXIMO 360P. NO CAMBIA CALIDAD DURANTE EL PLAY.", 2, muted);
    }
    else if (nav.screen == tube5::Screen::online)
    {
        canvas.text(120, 310, "YOUTUBE DIRECTO - SIN PUENTE PC", 4, Color::white);
        char identity[64]{};
        std::snprintf(identity, sizeof(identity), "VIDEO %s - CALIDAD %s", direct_video_id.data(),
                      direct_height ? "MANUAL" : "AUTO MAX 360P");
        canvas.text(120, 375, identity, 2, muted);
        const auto stage = probe.stage();
        const char *status =
            stage == tube5::ProbeStage::dns         ? "DNS: RESOLVIENDO WWW.YOUTUBE.COM"
            : stage == tube5::ProbeStage::https     ? "HTTPS: CONSULTANDO FORMATOS YOUTUBE"
            : stage == tube5::ProbeStage::source    ? "HTTPS: LEYENDO 4 KIB DEL STREAM MP4"
            : stage == tube5::ProbeStage::sponsor   ? "HTTPS: CONSULTANDO API SPONSORBLOCK"
            : stage == tube5::ProbeStage::ready     ? "OK: FUENTE MP4 RECIBIDA DESDE YOUTUBE"
            : stage == tube5::ProbeStage::failed    ? "FALLO: CAPTURA ESTA PANTALLA"
            : stage == tube5::ProbeStage::cancelled ? "CANCELADO"
                                                    : "X INICIAR PRUEBA";
        canvas.text(120, 430, status, 3,
                    stage == tube5::ProbeStage::failed ? Color::yellow : Color::white);
        if (!probe.loading() && stage != tube5::ProbeStage::idle)
        {
            const auto &result = probe.result();
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "IP %s  DNS RET 0X%08X", result.ip.data(),
                          static_cast<unsigned>(result.dns_error));
            canvas.text(120, 505, detail, 3, muted);
            std::snprintf(detail, sizeof(detail), "HTTP %ld  CURL %d  CERT %ld  BYTES %u",
                          result.http, result.curl_error, result.verify, result.bytes);
            canvas.text(120, 565, detail, 3, muted);
            const auto &formats = result.formats;
            std::snprintf(detail, sizeof(detail),
                          "FORMATOS %u  URL %u  CIPHER %u  MAX %uP  HLS %d DASH %d", formats.count,
                          formats.direct, formats.cipher, formats.max_height, formats.hls,
                          formats.dash);
            canvas.text(120, 625, detail, 2, muted);
            std::snprintf(detail, sizeof(detail), "AVC %u  VP9 %u  AV1 %u  AAC %u  OPUS %u",
                          formats.avc, formats.vp9, formats.av1, formats.aac, formats.opus);
            canvas.text(120, 675, detail, 2, muted);
            std::snprintf(detail, sizeof(detail),
                          "STREAM ITAG %u %uP AAC %u  HTTP %ld CURL %d CERT %ld  BYTES %u MP4 %s",
                          formats.selected_itag, formats.selected_height, formats.audio_itag,
                          result.source_http, result.source_curl, result.source_verify,
                          result.source_bytes, result.source_mp4 ? "SI" : "NO");
            canvas.text(120, 725, detail, 2, result.source_mp4 ? Color::white : Color::yellow);
            canvas.text(120, 775, result.title.data(), 2, Color::white);
            canvas.text(120, 825, result.error.data(), 2, Color::yellow);
            if (!error[0])
            {
                std::snprintf(detail, sizeof(detail),
                              "SB API: HTTP %ld CURL %d CERT %ld SEG %u %.60s", result.sponsor.http,
                              result.sponsor.curl_error, result.sponsor.verify,
                              result.sponsor.data.count, result.sponsor.error.data());
                canvas.text(120, 865, detail, 1, muted);
            }
        }
        if (!error[0])
            canvas.text(120, 925, "OPTIONS CALIDAD - R1 CAMBIAR VIDEO - TLS VERIFICADO", 2, muted);
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
        char storage_status[96]{};
        if (preference_saved)
            std::snprintf(storage_status, sizeof(storage_status), "PREFERENCIA PERSISTENTE");
        else
            std::snprintf(storage_status, sizeof(storage_status), "NO GUARDADO: %s E%d",
                          preferences.stage(), preferences.error());
        canvas.text(650, 630, storage_status, 2, preference_saved ? muted : Color::yellow);
        canvas.text(120, 750, "VIDEOS: MENU PRINCIPAL - REPRODUCIR", 3, muted);
#ifdef TUBE5_HW_PROBE
        for (unsigned row = 0; row < 4; ++row)
        {
            char status[192]{};
            tube5::hardware_video_status(hardware_report, row, status, sizeof(status));
            canvas.text(120, 815 + row * 32, status, 2, muted);
        }
#endif
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
                          media.cancelling()       ? "CANCELANDO"
                          : media.youtube_direct() ? "ABRIENDO YOUTUBE HTTPS"
                                                   : "ABRIENDO VIDEO HTTP",
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
        // Keep marks visible after the playhead passes and while auto-skip is OFF.
        if (sponsor_skip.data().state == tube5::SponsorState::ready)
            for (unsigned i = 0; i < sponsor_skip.data().count; ++i)
            {
                const auto &range = sponsor_skip.data().ranges[i];
                const unsigned left =
                    tube5::playback_progress_width(range.start_ms / 1000.0, media.duration(), 1220);
                const unsigned right =
                    tube5::playback_progress_width(range.end_ms / 1000.0, media.duration(), 1220);
                if (left < 1220 && range.end_ms > range.start_ms)
                    canvas.rectangle(350 + left, 885, std::max(1u, right - left), 8,
                                     static_cast<Color>(0xff3773ff));
            }
        canvas.rectangle(350 + std::min(progress, 1218u), 883, 2, 12, Color::white);
        if (!error[0] && !selection.active())
        {
            char status[128]{};
            if (sponsor_notice.visible(SDL_GetTicks()))
                std::snprintf(status, sizeof(status), "PATROCINIO OMITIDO - %u SEG",
                              sponsor_notice.seconds());
            else if (sponsor_skip.data().state == tube5::SponsorState::unavailable)
                std::snprintf(status, sizeof(status), "SB NO DISPONIBLE - VIDEO SIN SALTOS");
            else if (sponsor_skip.data().state == tube5::SponsorState::unaligned)
                std::snprintf(status, sizeof(status), "SB DURACION NO COINCIDE - VIDEO SIN SALTOS");
            else
                std::snprintf(status, sizeof(status), "SB %s  %u SEG  %u SALTOS  %s",
                              sponsor_enabled ? "ON" : "OFF", sponsor_skip.data().count,
                              sponsor_skip.skipped(),
                              preference_saved ? "OPTIONS CAMBIAR" : "NO GUARDADO");
            canvas.text(350, 930, status, 2,
                        sponsor_notice.visible(SDL_GetTicks()) ? Color::white : muted);
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
        if (media.youtube_direct())
            media.youtube_status(diagnostics, sizeof(diagnostics));
        else
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
    if (!error[0] && nav.screen == tube5::Screen::library && sponsor_notice.visible(SDL_GetTicks()))
    {
        char notice[80]{};
        std::snprintf(notice, sizeof(notice), "PATROCINIO OMITIDO - %u SEG - VIDEO FINALIZADO",
                      sponsor_notice.seconds());
        canvas.text(120, 900, notice, 2, muted);
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
        if (!playing_screen && !media.opening() && nav.screen != tube5::Screen::video_input &&
            nav.screen != tube5::Screen::quality)
        {
            // Keep the allocation and demux failure readable after teardown.
            // Retrying a fatal read ENOMEM no longer resets these counters.
            char failure[192]{};
            canvas.text(800, 185, media.failure_log_status(), 2, Color::yellow);
            // close() clears youtube_direct(); the numeric snapshots survive.
            if (std::strstr(error, "YT HTTPS"))
            {
                tube5::memory_diagnostic_status(failure, sizeof(failure));
                canvas.text(800, 155, failure, 2, Color::yellow);
                canvas.rectangle(100, 890, 1660, 140, panel);
                for (unsigned row = 0; row < 6; ++row)
                {
                    media.youtube_diagnostic(row, failure, sizeof(failure));
                    canvas.text(120, 900 + row * 22, failure, 2, Color::yellow);
                }
            }
            else
            {
                canvas.rectangle(100, 890, 1660, 100, panel);
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
#ifdef TUBE5_PARSER_BUFFER_WRAP
                tube5::native_parser_buffer_status(failure, sizeof(failure));
                canvas.text(800, 125, failure, 2, Color::yellow);
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
        }
#endif
    }
    canvas.text(
        120, nav.screen == tube5::Screen::player || error[0] ? 1040 : 995,
        nav.screen == tube5::Screen::player
            ? (selection.active() ? "IZQ/DER 5 SEG  L1/R1 30 SEG  X CONFIRMAR  CIRCULO CANCELAR"
                                  : "X PAUSA  IZQ/DER 5 SEG  TRIANGULO BUSCAR  CIRCULO VOLVER")
            : (nav.screen == tube5::Screen::online
                   ? "X CONSULTAR / REPRODUCIR  TRIANGULO RENOVAR  CIRCULO VOLVER"
               : nav.screen == tube5::Screen::video_input
                   ? "X ESCRIBIR  OPTIONS BORRAR  L1 LIMPIAR  TRIANGULO ABRIR  CIRCULO VOLVER"
               : nav.screen == tube5::Screen::quality
                   ? "CRUCETA ELEGIR  X APLICAR  TRIANGULO AUTO  CIRCULO VOLVER"
               : nav.screen == tube5::Screen::search ? "CRUCETA MOVER   X ELEGIR   CIRCULO VOLVER"
               : nav.screen == tube5::Screen::library
                   ? "X REPRODUCIR  TRIANGULO BUSCAR  OPTIONS RECARGAR  CIRCULO VOLVER"
                   : "CRUCETA MOVER   X ABRIR   CIRCULO VOLVER   OPTIONS AJUSTES"),
        nav.screen == tube5::Screen::video_input ? 2 : 3, muted);
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
    const auto preference_result = preferences.load(sponsor_enabled);
    preference_saved = preference_result == tube5::Preferences::Load::loaded ||
                       preference_result == tube5::Preferences::Load::missing;
    tube5_boot_checkpoint("Tube5 D03: MAIN - leer etiqueta");
    ps5::demo::read_asset_text("/app0/build-label.txt", std::span{build_label}, "");
    for (char &character : build_label)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    tube5::failure_log_build_label(build_label.data());
#ifdef TUBE5_HW_PROBE
    tube5::query_hardware_video(hardware_report);
    char hardware_log[1024]{}, hardware_line[192]{};
    std::size_t used = static_cast<std::size_t>(
        std::snprintf(hardware_log, sizeof(hardware_log),
                      "TUBE5 HARDWARE QUERY V1\nBUILD %s\nPLAYER SOFTWARE - NO DECODE TEST\n",
                      tube5::failure_log_build_label()));
    for (unsigned row = 0; row < 4; ++row)
    {
        tube5::hardware_video_status(hardware_report, row, hardware_line, sizeof(hardware_line));
        const int length =
            std::snprintf(hardware_log + used, sizeof(hardware_log) - used, "%s\n", hardware_line);
        if (length > 0)
            used += std::min<std::size_t>(length, sizeof(hardware_log) - used - 1);
    }
    for (unsigned i = 0; i < hardware_report.memory.size(); ++i)
    {
        const auto &memory = hardware_report.memory[i];
        const int length = std::snprintf(hardware_log + used, sizeof(hardware_log) - used,
                                         "MODE %u CPU %llu GPU %llu SHARED %llu\n", i,
                                         static_cast<unsigned long long>(memory.cpu_bytes),
                                         static_cast<unsigned long long>(memory.gpu_bytes),
                                         static_cast<unsigned long long>(memory.shared_bytes));
        if (length > 0)
            used += std::min<std::size_t>(length, sizeof(hardware_log) - used - 1);
    }
    (void)tube5::write_failure_log("/download0/tube5-hardware.log", hardware_log, used);
#if defined(TUBE5_HW_FRAME) && !defined(TUBE5_HW_CLIP)
    if (hardware_report.module == 0 && hardware_report.compute == 0 &&
        hardware_report.query[0] == 0)
        (void)tube5::hardware_frame_cycles("/app0/assets/hw-frame.h264", "/download0");
#endif
#endif
#ifdef TUBE5_HOST
    ps5::demo::read_asset_text(".local/network-test-url.txt", std::span{network_url}, "");
    ps5::demo::read_asset_text(".local/bridge/console-token", std::span{console_token}, "");
#else
    ps5::demo::read_asset_text("/app0/network-test-url.txt", std::span{network_url}, "");
    ps5::demo::read_asset_text("/app0/console-token.txt", std::span{console_token}, "");
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
#ifndef TUBE5_HOST
#ifdef TUBE5_HW_FRAME
    HardwarePreview hardware_preview;
#ifdef TUBE5_HW_CLIP
    Uint64 hardware_clip_started_at = 0, hardware_clip_next_at = 0;
#else
    Uint64 hardware_preview_until = 0;
#endif
    if (hardware_report.module == 0 && hardware_report.compute == 0 &&
        hardware_report.query[0] == 0)
    {
        hardware_preview.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                                     SDL_TEXTUREACCESS_STREAMING, 1920, 1080);
#ifdef TUBE5_HW_CLIP
        if (hardware_preview.texture &&
            tube5::hardware_clip_begin("/app0/assets/hw-clip.t5hc",
                                       "/download0/tube5-hardware-clip.log") == 0)
            hardware_clip_next_at = SDL_GetTicks64();
#else
        if (hardware_preview.texture &&
            tube5::hardware_frame_probe("/app0/assets/hw-frame.h264",
                                        "/download0/tube5-hardware-display.log",
                                        capture_hardware_nv12, &hardware_preview) == 0)
            hardware_preview_until = SDL_GetTicks64() + 5000;
#endif
        else if (hardware_preview.texture)
        {
            SDL_DestroyTexture(hardware_preview.texture);
            hardware_preview.texture = nullptr;
        }
#ifdef TUBE5_HW_CLIP
        else
        {
            char report[256]{};
            const int length = std::snprintf(
                report, sizeof(report),
                "TUBE5 HARDWARE CLIP V1\nBUILD %s\nSTAGE TEXTURE ERROR %08X\nSDL %s\n",
                tube5::failure_log_build_label(), static_cast<unsigned>(-ENOMEM), SDL_GetError());
            if (length > 0)
                (void)tube5::write_failure_log("/download0/tube5-hardware-clip.log", report,
                                               std::min<std::size_t>(length, sizeof(report) - 1));
        }
#endif
    }
#endif
#endif
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
    tube5::YouTubeSearch youtube_search;
    tube5::YouTubeProbe youtube_probe;
    tube5::YouTubeSearch::Request search_request = tube5::YouTubeSearch::Request::search;
    Uint64 search_started = 0, search_poll_at = 0;
    std::array<char, 49> requested_entry{};
    unsigned library_selected = 0;
    bool library_playback = false;
    bool direct_playback = false;
    const auto playback_return = [&]()
    {
        return direct_playback    ? tube5::Screen::online
               : library_playback ? tube5::Screen::library
                                  : tube5::Screen::sources;
    };
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
        sponsor_notice.cancel();
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
        const bool configured = direct_playback || !network_test ||
                                (source && std::string_view{source}.starts_with("http://"));
        if (!kit_ready || !configured ||
            !((network_test || direct_playback)
                  ? media.begin_open(
                        source, direct_playback ? youtube_probe.result().formats.audio_source.data()
                                                : nullptr)
                  : media.open(renderer, clip)))
        {
            std::snprintf(error, sizeof(error), "%s",
                          !kit_ready    ? "PLAYER INIT FAILED"
                          : !configured ? "HTTP: CONFIGURA NETWORK-TEST-URL.TXT"
                                        : media.error());
            nav.screen = playback_return();
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
#if defined(TUBE5_HW_CLIP) && !defined(TUBE5_HOST)
        if (tube5::hardware_clip_status().active)
        {
            if (action == tube5::Command::back)
            {
                (void)tube5::hardware_clip_close(true);
                SDL_DestroyTexture(hardware_preview.texture);
                hardware_preview.texture = nullptr;
            }
            return;
        }
#endif
        if (nav.screen == tube5::Screen::sources)
        {
            if (action == tube5::Command::previous)
                source_selected = (source_selected + 2) % 3;
            else if (action == tube5::Command::next)
                source_selected = (source_selected + 1) % 3;
            else if (action == tube5::Command::back)
            {
                nav.screen = tube5::Screen::home;
                error[0] = 0;
            }
            else if (action == tube5::Command::select)
            {
                error[0] = 0;
                if (source_selected == 0)
                    nav.screen = tube5::Screen::online;
                else if (source_selected == 1)
                {
                    network_test = true;
                    nav.screen = tube5::Screen::library;
                    if (!catalog.data().count)
                        load_catalog();
                }
                else
                {
                    network_test = false;
                    direct_playback = library_playback = false;
                    nav.screen = tube5::Screen::player;
                    start_player(nullptr);
                }
            }
            else if (action == tube5::Command::settings)
                nav.screen = tube5::Screen::settings;
            return;
        }
        if (nav.screen == tube5::Screen::video_input)
        {
            if (action == tube5::Command::back)
            {
                error[0] = 0;
                nav.screen = tube5::Screen::online;
            }
            else if (action == tube5::Command::previous)
                video_keyboard.move(-1);
            else if (action == tube5::Command::next)
                video_keyboard.move(1);
            else if (action == tube5::Command::coarse_previous)
                video_keyboard.move(-9);
            else if (action == tube5::Command::coarse_next)
                video_keyboard.move(9);
            else if (action == tube5::Command::select)
                video_keyboard.choose();
            else if (action == tube5::Command::settings)
                video_keyboard.erase();
            else if (action == tube5::Command::search)
            {
                std::array<char, 12> id{};
                if (!tube5::youtube_input_id(video_keyboard.input(), id))
                    std::snprintf(error, sizeof(error), "ID/URL NO VALIDA - REVISA LAS MAYUSCULAS");
                else if (!youtube_probe.loading())
                {
                    direct_video_id = id;
                    direct_height = quality_selected = 0;
                    error[0] = 0;
                    nav.screen = tube5::Screen::online;
                    (void)youtube_probe.begin(tube5::ProbeMode::formats, id.data());
                }
            }
            return;
        }
        if (nav.screen == tube5::Screen::quality)
        {
            const auto &formats = youtube_probe.result().formats;
            if (action == tube5::Command::back)
            {
                error[0] = 0;
                nav.screen = tube5::Screen::online;
            }
            else if (formats.quality_count && action == tube5::Command::previous)
                quality_selected =
                    (quality_selected + formats.quality_count - 1) % formats.quality_count;
            else if (formats.quality_count && action == tube5::Command::next)
                quality_selected = (quality_selected + 1) % formats.quality_count;
            else if (action == tube5::Command::search ||
                     (action == tube5::Command::select && formats.quality_count))
            {
                const bool automatic = action == tube5::Command::search;
                if (!automatic && !formats.qualities[quality_selected].playable &&
                    !formats.qualities[quality_selected].candidate)
                    std::snprintf(error, sizeof(error),
                                  "CALIDAD PENDIENTE - ELIGE UNA OPCION LISTA");
                else
                {
                    direct_height = automatic ? 0 : formats.qualities[quality_selected].height;
                    error[0] = 0;
                    nav.screen = tube5::Screen::online;
                    (void)youtube_probe.begin(tube5::ProbeMode::formats, direct_video_id.data(),
                                              direct_height);
                }
            }
            return;
        }
        if (nav.screen == tube5::Screen::online)
        {
            if (action == tube5::Command::back)
            {
                youtube_probe.cancel();
                nav.screen = tube5::Screen::sources;
                error[0] = 0;
            }
            else if (!youtube_probe.loading())
            {
                if (action == tube5::Command::coarse_next)
                {
                    (void)video_keyboard.set(direct_video_id.data());
                    error[0] = 0;
                    nav.screen = tube5::Screen::video_input;
                }
                else if (action == tube5::Command::settings)
                {
                    quality_selected = 0;
                    const auto &formats = youtube_probe.result().formats;
                    for (unsigned i = 0; i < formats.quality_count; ++i)
                        if (formats.qualities[i].height == formats.selected_height)
                            quality_selected = i;
                    error[0] = 0;
                    nav.screen = tube5::Screen::quality;
                }
                else if (action == tube5::Command::search)
                {
                    error[0] = 0;
                    (void)youtube_probe.begin(tube5::ProbeMode::formats, direct_video_id.data(),
                                              direct_height);
                }
                else if (action == tube5::Command::select)
                {
                    if (youtube_probe.stage() == tube5::ProbeStage::ready &&
                        youtube_probe.result().source_mp4)
                    {
                        direct_playback = true;
                        library_playback = false;
                        nav.screen = tube5::Screen::player;
                        start_player(youtube_probe.result().formats.source.data());
                    }
                    else
                    {
                        error[0] = 0;
                        (void)youtube_probe.begin(tube5::ProbeMode::formats, direct_video_id.data(),
                                                  direct_height);
                    }
                }
            }
            return;
        }
        if (nav.screen == tube5::Screen::search)
        {
            if (action == tube5::Command::back)
            {
                youtube_search.cancel();
                error[0] = 0;
                if (search_page == SearchPage::results)
                    search_page = SearchPage::keyboard;
                else
                    nav.screen = tube5::Screen::library;
                return;
            }
            if (youtube_search.loading() || search_page == SearchPage::searching ||
                search_page == SearchPage::preparing)
                return;
            if (search_page == SearchPage::keyboard)
            {
                if (action == tube5::Command::previous)
                    search_keyboard.move(-1);
                else if (action == tube5::Command::next)
                    search_keyboard.move(1);
                else if (action == tube5::Command::coarse_previous)
                    search_keyboard.move(-10);
                else if (action == tube5::Command::coarse_next)
                    search_keyboard.move(10);
                else if (action == tube5::Command::select)
                    search_keyboard.choose();
                else if (action == tube5::Command::settings)
                    search_keyboard.erase();
                else if (action == tube5::Command::search)
                {
                    error[0] = 0;
                    search_request = tube5::YouTubeSearch::Request::search;
                    if (youtube_search.begin(network_url.data(), console_token.data(),
                                             search_request, search_keyboard.query()))
                    {
                        search_page = SearchPage::searching;
                        search_started = SDL_GetTicks64();
                    }
                    else
                        std::snprintf(error, sizeof(error), "%s",
                                      youtube_search.error()[0] ? youtube_search.error()
                                                                : "ESCRIBE UNA BUSQUEDA VALIDA");
                }
            }
            else if (search_page == SearchPage::results)
            {
                if (action == tube5::Command::search)
                {
                    search_page = SearchPage::keyboard;
                    error[0] = 0;
                }
                else if (search_results.count &&
                         (action == tube5::Command::previous || action == tube5::Command::next))
                    search_selected = (search_selected + search_results.count +
                                       (action == tube5::Command::previous ? -1 : 1)) %
                                      search_results.count;
                else if (action == tube5::Command::select && search_selected < search_results.count)
                {
                    error[0] = 0;
                    search_request = tube5::YouTubeSearch::Request::prepare;
                    if (youtube_search.begin(network_url.data(), console_token.data(),
                                             search_request,
                                             search_results.entries[search_selected].id.data()))
                    {
                        search_page = SearchPage::preparing;
                        search_started = SDL_GetTicks64();
                    }
                    else
                        std::snprintf(error, sizeof(error), "%s", youtube_search.error());
                }
            }
            return;
        }
        if (nav.screen == tube5::Screen::library)
        {
            if (action == tube5::Command::search && !catalog.loading())
            {
                nav.screen = tube5::Screen::search;
                search_page = SearchPage::keyboard;
                error[0] = 0;
                return;
            }
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
                nav.screen = tube5::Screen::sources;
                error[0] = 0;
            }
            else if (action == tube5::Command::select &&
                     catalog.media_url(library_selected, active_media_url.data(),
                                       active_media_url.size()))
            {
                library_playback = true;
                direct_playback = false;
                nav.screen = tube5::Screen::player;
                start_player(active_media_url.data());
            }
            return;
        }
        if (nav.screen == tube5::Screen::settings &&
            (action == tube5::Command::previous || action == tube5::Command::next))
        {
            toggle_sponsor();
            return;
        }
        if (nav.screen == tube5::Screen::player)
        {
            if (action == tube5::Command::settings)
            {
                toggle_sponsor();
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
                        sponsor_notice.cancel();
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
                sponsor_notice.cancel();
                media.seek(action == tube5::Command::previous ? -5.0 : 5.0);
            }
            else if (action == tube5::Command::back)
            {
                selection.cancel();
                sponsor_notice.cancel();
                media.close();
                nav.screen = playback_return();
            }
            return;
        }
        (void)nav.apply(action);
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
        else if (std::string_view{screen} == "player")
            nav.screen = tube5::Screen::player;
        else if (std::string_view{screen} == "online")
            nav.screen = tube5::Screen::online;
        else if (std::string_view{screen} == "sources")
            nav.screen = tube5::Screen::sources;
        else if (std::string_view{screen} == "video-input")
        {
            nav.screen = tube5::Screen::video_input;
            (void)video_keyboard.set(direct_video_id.data());
        }
        else if (std::string_view{screen} == "search" ||
                 std::string_view{screen} == "search-results")
        {
            nav.screen = tube5::Screen::search;
            if (std::string_view{screen} == "search-results")
            {
                search_page = SearchPage::results;
                search_results.count = 8;
                for (auto &entry : search_results.entries)
                {
                    std::snprintf(entry.title.data(), entry.title.size(),
                                  "RESULTADO DE PRUEBA - TITULO LARGO PARA COMPROBAR LA LISTA Y "
                                  "LOS CONTROLES");
                    std::snprintf(entry.channel.data(), entry.channel.size(), "CANAL DE PRUEBA");
                    entry.seconds = 887;
                }
            }
        }
    }
    if (std::getenv("TUBE5_PREVIEW_NOTICE"))
    {
        sponsor_notice.begin(0, 4);
        sponsor_notice.complete(SDL_GetTicks());
    }
    if (std::getenv("TUBE5_PREVIEW_SEGMENTS"))
    {
        network_test = false;
        nav.screen = tube5::Screen::player;
        start_player(nullptr);
        tube5::SponsorData fixture;
        fixture.state = tube5::SponsorState::ready;
        fixture.count = 2;
        fixture.ranges[0] = {0, 4000};
        fixture.ranges[1] = {15000, 20000};
        sponsor_skip.reset(fixture);
        sponsor_enabled = false;
        media.toggle_pause();
    }
    if (std::getenv("TUBE5_PREVIEW_ONLINE_RUN"))
    {
        nav.screen = tube5::Screen::online;
        (void)youtube_probe.begin(tube5::ProbeMode::formats);
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
                    command(tube5::Command::previous);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    command((nav.screen == tube5::Screen::search &&
                             search_page == SearchPage::keyboard) ||
                                    nav.screen == tube5::Screen::video_input
                                ? tube5::Command::coarse_previous
                                : tube5::Command::previous);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    command(tube5::Command::next);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    command((nav.screen == tube5::Screen::search &&
                             search_page == SearchPage::keyboard) ||
                                    nav.screen == tube5::Screen::video_input
                                ? tube5::Command::coarse_next
                                : tube5::Command::next);
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
                    if (nav.screen == tube5::Screen::video_input)
                        video_keyboard.clear();
                    else
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
                    command(tube5::Command::previous);
                    break;
                case SDLK_UP:
                    command((nav.screen == tube5::Screen::search &&
                             search_page == SearchPage::keyboard) ||
                                    nav.screen == tube5::Screen::video_input
                                ? tube5::Command::coarse_previous
                                : tube5::Command::previous);
                    break;
                case SDLK_RIGHT:
                    command(tube5::Command::next);
                    break;
                case SDLK_DOWN:
                    command((nav.screen == tube5::Screen::search &&
                             search_page == SearchPage::keyboard) ||
                                    nav.screen == tube5::Screen::video_input
                                ? tube5::Command::coarse_next
                                : tube5::Command::next);
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
                    if (nav.screen == tube5::Screen::video_input)
                        video_keyboard.clear();
                    else
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
        youtube_probe.poll();
#ifdef TUBE5_HOST
        if (std::getenv("TUBE5_PREVIEW_QUALITY") && !youtube_probe.loading() &&
            youtube_probe.stage() == tube5::ProbeStage::ready)
            nav.screen = tube5::Screen::quality;
#endif
        if (youtube_search.loading())
        {
            youtube_search.poll();
            if (!youtube_search.loading() && !youtube_search.cancelled() &&
                nav.screen == tube5::Screen::search)
            {
                const auto &result = youtube_search.data();
                if (youtube_search.error()[0] || result.state == tube5::SearchState::failed ||
                    result.state == tube5::SearchState::idle)
                {
                    std::snprintf(error, sizeof(error), "%s",
                                  youtube_search.error()[0] ? youtube_search.error()
                                                            : "BUSQUEDA O PREPARACION FALLIDA");
                    search_page = search_request == tube5::YouTubeSearch::Request::search
                                      ? SearchPage::keyboard
                                      : SearchPage::results;
                }
                else if (result.state == tube5::SearchState::ready)
                {
                    if (search_request == tube5::YouTubeSearch::Request::search)
                    {
                        search_results = result;
                        search_selected = 0;
                        search_page = SearchPage::results;
                    }
                    else
                    {
                        requested_entry = result.prepared_id;
                        nav.screen = tube5::Screen::library;
                        load_catalog();
                    }
                    SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
                    SDL_FlushEvent(SDL_KEYDOWN);
                    select_ready_at = SDL_GetTicks() + 350;
                }
                else
                    search_poll_at = SDL_GetTicks64() + 1000;
            }
        }
        if (nav.screen == tube5::Screen::search && !youtube_search.loading() &&
            (search_page == SearchPage::searching || search_page == SearchPage::preparing) &&
            SDL_GetTicks64() >= search_poll_at)
        {
            const bool searching = search_page == SearchPage::searching;
            if (SDL_GetTicks64() - search_started > (searching ? 60000u : 2500000u))
            {
                std::snprintf(error, sizeof(error), "TIEMPO DE ESPERA AGOTADO");
                search_page = searching ? SearchPage::keyboard : SearchPage::results;
            }
            else
            {
                search_request = searching ? tube5::YouTubeSearch::Request::search
                                           : tube5::YouTubeSearch::Request::job;
                if (!youtube_search.begin(network_url.data(), console_token.data(), search_request,
                                          searching ? search_keyboard.query() : ""))
                {
                    std::snprintf(error, sizeof(error), "%s", youtube_search.error());
                    search_page = searching ? SearchPage::keyboard : SearchPage::results;
                }
            }
        }
        if (media.opening())
        {
            const auto result = media.poll_open(renderer);
            if (result == tube5::MediaPlayer::OpenResult::failed)
            {
                std::snprintf(error, sizeof(error), "%s", media.error());
                nav.screen = playback_return();
            }
            else if (result == tube5::MediaPlayer::OpenResult::cancelled)
                nav.screen = playback_return();
            else if (result == tube5::MediaPlayer::OpenResult::ready)
            {
                if (direct_playback)
                    sponsor_skip.reset(
                        tube5::aligned_sponsor(youtube_probe.result().sponsor, media.duration()));
                // Evaluate the known start, never the uninitialized player clock.
                if (const auto target =
                        sponsor_skip.target(0.0, media.duration(), sponsor_enabled, false, false))
                {
                    sponsor_notice.begin(0.0, *target);
                    if (*target >= media.duration())
                    {
                        sponsor_notice.complete(SDL_GetTicks());
                        media.close();
                        nav.screen = playback_return();
                    }
                    else
                        media.seek_sponsor(*target);
                }
                SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
                SDL_FlushEvent(SDL_KEYDOWN);
                select_ready_at = SDL_GetTicks() + 350;
            }
        }
        if (nav.screen == tube5::Screen::player && media.check_network_failure())
        {
            std::snprintf(error, sizeof(error), "%s", media.error());
            nav.screen = playback_return();
        }
        if (nav.screen == tube5::Screen::player && media.finished())
        {
            media.close();
            nav.screen = playback_return();
        }
        if (nav.screen == tube5::Screen::player && !media.opening())
            if (const auto target = sponsor_skip.target(
                    media.position(), media.duration(), sponsor_enabled, media.paused(),
                    media.seeking() || selection.active() || media.frames_uploaded() == 0))
            {
                sponsor_notice.begin(media.position(), *target);
                if (*target >= media.duration())
                {
                    sponsor_notice.complete(SDL_GetTicks());
                    media.close();
                    nav.screen = playback_return();
                }
                else
                    media.seek_sponsor(*target);
            }
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
                else if (requested_entry[0])
                {
                    for (unsigned i = 0; i < catalog.data().count; ++i)
                        if (std::string_view{catalog.data().entries[i].id.data()} ==
                            requested_entry.data())
                            library_selected = i;
                    requested_entry = {};
                }
                SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
                SDL_FlushEvent(SDL_KEYDOWN);
            }
        }
#if defined(TUBE5_HW_CLIP) && !defined(TUBE5_HOST)
        if (tube5::hardware_clip_status().active && SDL_GetTicks64() >= hardware_clip_next_at)
        {
            if (!hardware_clip_started_at)
                hardware_clip_started_at = SDL_GetTicks64();
            const int result = tube5::hardware_clip_step(capture_hardware_nv12, &hardware_preview);
            if (result == 0)
                hardware_clip_next_at =
                    hardware_clip_started_at +
                    static_cast<Uint64>(tube5::hardware_clip_status().submitted) * 1000 / 30;
            else
            {
                (void)tube5::hardware_clip_close();
                SDL_DestroyTexture(hardware_preview.texture);
                hardware_preview.texture = nullptr;
            }
        }
        Uint64 hardware_copy_started = 0;
#endif
        if (output && SDL_LockSurface(output) != 0)
            fatal("UI SURFACE LOCK");
        const Uint32 paint_start = SDL_GetTicks();
        paint(canvas, nav, media, controller != nullptr, error, selection, catalog,
              library_selected, sponsor_skip, youtube_probe);
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
            if (!media.opening() && !media.seeking() && media.frames_presented() &&
                !media.error()[0])
                sponsor_notice.complete(SDL_GetTicks());
            if (media.error()[0])
                sponsor_notice.cancel();
            if (!media.opening() && media.error()[0])
                std::snprintf(error, sizeof(error), "%s", media.error());
        }
#ifndef TUBE5_HOST
#ifdef TUBE5_HW_FRAME
        if (hardware_preview.texture)
        {
#ifdef TUBE5_HW_CLIP
            if (tube5::hardware_clip_status().active && tube5::hardware_clip_status().decoded)
#else
            if (hardware_preview_until && SDL_GetTicks64() < hardware_preview_until)
#endif
            {
#ifdef TUBE5_HW_CLIP
                hardware_copy_started = SDL_GetPerformanceCounter();
#endif
                const SDL_Rect preview_area{350, 260, 1220, 686};
                if (SDL_RenderCopy(renderer, hardware_preview.texture, nullptr, &preview_area) != 0)
                {
#ifdef TUBE5_HW_CLIP
                    (void)tube5::hardware_clip_close(false, -EIO);
#endif
                    SDL_DestroyTexture(hardware_preview.texture);
                    hardware_preview.texture = nullptr;
                }
            }
            else
            {
                SDL_DestroyTexture(hardware_preview.texture);
                hardware_preview.texture = nullptr;
            }
        }
#endif
#endif
        const Uint32 video_end = SDL_GetTicks();
        SDL_RenderPresent(renderer);
#ifdef TUBE5_HOST
        static bool first_frame = true;
        if (first_frame)
        {
            tube5_boot_checkpoint("Tube5 D13: primer frame presentado");
            first_frame = false;
        }
        if (const char *preview = std::getenv("TUBE5_PREVIEW");
            preview && !catalog.loading() && !youtube_probe.loading())
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
#if defined(TUBE5_HW_CLIP) && !defined(TUBE5_HOST)
        if (hardware_copy_started)
            tube5::hardware_clip_presented((SDL_GetPerformanceCounter() - hardware_copy_started) *
                                           1000000 / SDL_GetPerformanceFrequency());
#endif
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
    sponsor_notice.cancel();
    youtube_probe.cancel();
    while (youtube_probe.loading())
    {
        youtube_probe.poll();
        SDL_Delay(5);
    }
    youtube_search.cancel();
    while (youtube_search.loading())
    {
        youtube_search.poll();
        SDL_Delay(5);
    }
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
