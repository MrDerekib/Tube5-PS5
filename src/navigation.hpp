// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace tube5
{
enum class Screen
{
    home,
    settings,
    about,
    player,
    library,
    search,
    online,
    video_input,
    quality,
    sources
};
enum class Command
{
    previous,
    next,
    select,
    back,
    settings,
    search,
    coarse_previous,
    coarse_next
};

struct Navigation
{
    Screen screen = Screen::home;
    int selected = 0;

    // Home opens the source picker; playback requires an explicit source choice.
    bool apply(Command command) noexcept
    {
        if (command == Command::back)
        {
            screen = Screen::home;
            return false;
        }
        if (command == Command::settings && screen != Screen::player)
        {
            screen = screen == Screen::settings ? Screen::home : Screen::settings;
            return false;
        }
        if (screen != Screen::home)
            return false;
        if (command == Command::previous)
            selected = (selected + 2) % 3;
        else if (command == Command::next)
            selected = (selected + 1) % 3;
        else if (command == Command::select)
        {
            screen = selected == 0   ? Screen::sources
                     : selected == 1 ? Screen::settings
                                     : Screen::about;
            return false;
        }
        return false;
    }
};
} // namespace tube5
