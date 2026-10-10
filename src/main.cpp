/*
 * ps5-native-app-boilerplate - Minimal native application example.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Draws a small CPU-rendered scene and keeps the process alive for normal
 * shell-mediated closure.
 */

#include "demo_renderer.hpp"

#include <array>
#include <span>

namespace
{
std::array<char, 48> banner{};
std::array<char, 48> build_label{};

void draw_scene(ps5::demo::Canvas &canvas) noexcept
{
    using ps5::demo::Color;

    canvas.clear(Color::background);
    canvas.rectangle(120, 430, 1680, 470, Color::panel);
    canvas.rectangle(120, 375, 1680, 8, Color::cyan);

    canvas.text(120, 90, "TUBE5", 14, Color::white);
    canvas.text(120, 245, "PS5 MEDIA HOME BREW", 6, Color::white);
    canvas.text(120, 315, banner.data(), 5, Color::cyan);

    canvas.rectangle(220, 530, 1480, 270, Color::background);
    canvas.triangle(850, 585, 160, 160, Color::magenta);
    canvas.text(450, 840, "FOUNDATION BUILD", 6, Color::white);

    // A build that is not a release says which one it is (docs/PULL_REQUEST_BUILDS.md).
    if (build_label[0] != '\0')
        canvas.text(120, 960, build_label.data(), 4, Color::yellow);
}
} // namespace

int main()
{
    ps5::demo::read_asset_text("/app0/assets/banner.txt", std::span{banner}, "APP0 ASSET FAILED");
    // Written by the build when BUILD_LABEL is set; the demo font has capitals only.
    ps5::demo::read_asset_text("/app0/build-label.txt", std::span{build_label}, "");
    for (char &character : build_label)
    {
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    }
    ps5::demo::run(draw_scene, banner.data());
}
