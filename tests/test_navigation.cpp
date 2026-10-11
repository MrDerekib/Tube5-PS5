// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigation.hpp"
#include <gtest/gtest.h>

using tube5::Command;
using tube5::Screen;

TEST(Navigation, FocusWrapsAndBackPreservesSelection)
{
    tube5::Navigation nav;
    EXPECT_FALSE(nav.apply(Command::previous));
    EXPECT_EQ(nav.selected, 2);
    EXPECT_FALSE(nav.apply(Command::select));
    EXPECT_EQ(nav.screen, Screen::about);
    nav.apply(Command::back);
    EXPECT_EQ(nav.screen, Screen::home);
    EXPECT_EQ(nav.selected, 2);
    nav.apply(Command::next);
    EXPECT_EQ(nav.selected, 0);
}

TEST(Navigation, PlaybackOnlyStartsFromTheVideoCard)
{
    tube5::Navigation nav;
    nav.apply(Command::next);
    EXPECT_FALSE(nav.apply(Command::select));
    EXPECT_EQ(nav.screen, Screen::settings);
    EXPECT_FALSE(nav.apply(Command::select));
    nav.apply(Command::back);
    nav.apply(Command::previous);
    EXPECT_TRUE(nav.apply(Command::select));
    EXPECT_EQ(nav.screen, Screen::player);
    EXPECT_FALSE(nav.apply(Command::select));
    EXPECT_FALSE(nav.apply(Command::settings));
    EXPECT_EQ(nav.screen, Screen::player);
    nav.apply(Command::back);
    EXPECT_EQ(nav.screen, Screen::home);
}

TEST(Navigation, SearchInputsDoNotChangeMenuFocus)
{
    tube5::Navigation nav;
    for (auto command : {Command::search, Command::coarse_previous, Command::coarse_next})
    {
        EXPECT_FALSE(nav.apply(command));
        EXPECT_EQ(nav.selected, 0);
        EXPECT_EQ(nav.screen, Screen::home);
    }
    nav.apply(Command::next);
    nav.apply(Command::select);
    EXPECT_FALSE(nav.apply(Command::search));
    EXPECT_EQ(nav.screen, Screen::settings);
}
