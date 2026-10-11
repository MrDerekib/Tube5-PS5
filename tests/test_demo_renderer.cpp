/*
 * ps5-native-app-boilerplate - Host tests for reusable application behavior.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Validates bounded asset loading without requiring PS5 hardware.
 */

#include "demo_renderer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <span>
#include <vector>

namespace
{
TEST(CanvasTest, LowercaseTextUsesVisibleCaseSensitiveGlyphs)
{
    std::vector<std::uint32_t> mixed(1920 * 1080, 0), upper(1920 * 1080, 0);
    ps5::demo::Canvas mixed_canvas{mixed.data(), true}, upper_canvas{upper.data(), true};
    mixed_canvas.text(120, 400, "Abc Youtube Video", 2, ps5::demo::Color::white);
    upper_canvas.text(120, 400, "ABC YOUTUBE VIDEO", 2, ps5::demo::Color::white);
    EXPECT_NE(mixed, upper);
    bool visible = false;
    for (const auto pixel : mixed)
        visible = visible || pixel != 0;
    EXPECT_TRUE(visible);
}

TEST(CanvasTest, BlitAcceptsRowPitchAndRejectsShortSources)
{
    constexpr unsigned pitch = 1928;
    constexpr std::uint32_t guard = 0x12345678;
    std::vector<std::uint32_t> source(pitch * 1080, 0);
    for (unsigned y = 0; y < 1080; ++y)
        for (unsigned x = 0; x < 1920; ++x)
            source[y * pitch + x] = 0xff000000 | (y << 11) | x;
    std::vector<std::uint32_t> linear(1920 * 1080 + 2, guard);
    ps5::demo::Canvas canvas{linear.data() + 1, true};
    EXPECT_FALSE(canvas.blit_linear(std::span{source}.first(100), pitch));
    EXPECT_FALSE(canvas.blit_linear(source, 1919));
    EXPECT_EQ(linear[1], guard);
    ASSERT_TRUE(canvas.blit_linear(source, pitch));
    EXPECT_EQ(linear[1], source[0]);
    EXPECT_EQ(linear[1920 + 1], source[pitch]);
    EXPECT_EQ(linear[1920 * 1080], source[1079 * pitch + 1919]);
    EXPECT_EQ(linear.front(), guard);
    EXPECT_EQ(linear.back(), guard);

    std::vector<std::uint32_t> tiled(0x1000000 / 4 + 2, guard);
    ps5::demo::Canvas native{tiled.data() + 1};
    ASSERT_TRUE(native.blit_linear(source, pitch));
    // Fixed address fixtures for the existing PS5 tiled layout.
    EXPECT_EQ(tiled[1], source[0]);
    EXPECT_EQ(tiled[1 + 1], source[1]);
    EXPECT_EQ(tiled[4 + 1], source[pitch]);
    EXPECT_EQ(tiled[15423 + 1], source[127 * pitch + 127]);
    EXPECT_EQ(tiled[16384 + 1], source[128]);
    EXPECT_EQ(tiled[2206335 + 1], source[1079 * pitch + 1919]);
    // Every visible pixel must retain the original address mapping, including
    // the partial bottom tile. A few fixtures cannot catch a tile permutation.
    for (unsigned y = 0; y < 1080; ++y)
        for (unsigned x = 0; x < 1920; ++x)
        {
            const unsigned offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^
                                    ((y << 9) & 0x1000U) ^ ((y << 8) & 0x4000U) ^
                                    ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^ ((x << 4) & 0x400U) ^
                                    ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
            const std::size_t address = ((y / 128) * 15 + x / 128) * 16384 + offset / 4;
            ASSERT_EQ(tiled[address + 1], source[y * pitch + x]) << x << "," << y;
        }
    EXPECT_EQ(tiled.front(), guard);
    EXPECT_EQ(tiled.back(), guard);
}

TEST(CanvasTest, LinearFramebufferStaysInsideBounds)
{
    constexpr std::size_t width = 1920, height = 1080;
    constexpr std::uint32_t guard = 0x12345678;
    std::vector<std::uint32_t> pixels(width * height + 2, guard);
    ps5::demo::Canvas canvas{pixels.data() + 1, true};
    canvas.clear(ps5::demo::Color::background);
    EXPECT_EQ(pixels[1], static_cast<std::uint32_t>(ps5::demo::Color::background));
    EXPECT_EQ(pixels[width * height], static_cast<std::uint32_t>(ps5::demo::Color::background));
    canvas.rectangle(1918, 1078, 100, 100, ps5::demo::Color::white);
    canvas.circle(1919, 1079, 30, ps5::demo::Color::white);
    canvas.triangle(1919, 1070, 30, 30, ps5::demo::Color::white);
    canvas.text(1919, 1079, "TUBE5", 4, ps5::demo::Color::white);
    EXPECT_EQ(pixels.front(), guard);
    EXPECT_EQ(pixels.back(), guard);
    EXPECT_EQ(pixels[width * height], static_cast<std::uint32_t>(ps5::demo::Color::white));
}

class AssetTextTest : public testing::Test
{
  protected:
    static constexpr const char *missing_path = "build/tests/missing-banner.txt";
    static constexpr const char *asset_path = "build/tests/banner.txt";

    void SetUp() override
    {
        (void)std::remove(missing_path);
        std::ofstream asset{asset_path, std::ios::binary};
        ASSERT_TRUE(asset.is_open());
        asset << "FROM APP0\r\n";
        ASSERT_TRUE(asset.good());
    }

    void TearDown() override
    {
        (void)std::remove(asset_path);
        (void)std::remove(missing_path);
    }
};

TEST_F(AssetTextTest, MissingAssetUsesFallback)
{
    std::array<char, 32> text{};
    ps5::demo::read_asset_text(missing_path, std::span{text}, "FALLBACK");
    EXPECT_STREQ(text.data(), "FALLBACK");
}

TEST_F(AssetTextTest, AssetTextTrimsLineEndings)
{
    std::array<char, 32> text{};
    ps5::demo::read_asset_text(asset_path, std::span{text}, "FALLBACK");
    EXPECT_STREQ(text.data(), "FROM APP0");
}

TEST_F(AssetTextTest, AssetTextRemainsNullTerminatedWhenTruncated)
{
    std::array<char, 5> bounded{};
    ps5::demo::read_asset_text(asset_path, std::span{bounded}, "FALLBACK");
    EXPECT_STREQ(bounded.data(), "FROM");
}
} // namespace
