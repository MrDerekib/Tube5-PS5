// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_search.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main(int argc, char **argv)
{
    tube5::SearchData data;
    const std::string valid = "TUBE5_SEARCH\t1\tready\nbHIhgxav9LY\tELECTRICITY\tVERITASIUM\t887\n";
    assert(tube5::parse_search_reply(valid, data) && data.count == 1 &&
           data.entries[0].seconds == 887);
    for (const auto &bad :
         {std::string{}, std::string(2049, 'a'), valid.substr(0, valid.size() - 1),
          valid + "bHIhgxav9LY\tDUPLICATE\tCHANNEL\t887\n",
          std::string("TUBE5_SEARCH\t1\tsearching\na\tb\tc\t20\n"),
          std::string("TUBE5_SEARCH\t1\tready\nbHIhgxav9LY\tX\tY\t99999999\n"),
          std::string("TUBE5_JOB\t1\tready\t-\n"),
          std::string("TUBE5_JOB\t1\tready\tyt-bHIhgxav9LY-720-60\n")})
        assert(!tube5::parse_search_reply(bad, data) && data.count == 0);
    assert(tube5::parse_search_reply("TUBE5_SEARCH\t1\tsearching\n", data));
    assert(data.state == tube5::SearchState::searching);
    assert(tube5::parse_search_reply("TUBE5_JOB\t1\tready\tyt-bHIhgxav9LY-360-60\n", data));
    assert(std::strcmp(data.prepared_id.data(), "yt-bHIhgxav9LY-360-60") == 0);
    tube5::SearchKeyboard keyboard;
    keyboard.move(-1);
    assert(keyboard.selected() == 39);
    keyboard.move(1);
    assert(keyboard.selected() == 0);
    keyboard.move(-4000);
    assert(keyboard.selected() == 0);
    for (int i = 0; i < 60; ++i)
        keyboard.choose();
    assert(std::strlen(keyboard.query()) == 48);
    keyboard.erase();
    assert(std::strlen(keyboard.query()) == 47);
    if (argc < 3)
        return 0;
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    {
        tube5::YouTubeSearch client;
        const std::string token(64, 'a');
        const bool cancel = std::strcmp(argv[2], "cancel") == 0;
        const bool failure = std::strcmp(argv[2], "failure") == 0;
        const bool prepare = std::strcmp(argv[2], "prepare") == 0;
        const auto start = SDL_GetTicks64();
        assert(client.begin(argv[1], token.c_str(),
                            prepare ? tube5::YouTubeSearch::Request::prepare
                                    : tube5::YouTubeSearch::Request::search,
                            prepare ? "bHIhgxav9LY" : "VERITASIUM ELECTRICITY"));
        assert(SDL_GetTicks64() - start < 250);
        if (cancel)
        {
            SDL_Delay(30);
            client.cancel();
        }
        while (client.loading())
        {
            client.poll();
            SDL_Delay(5);
            assert(SDL_GetTicks64() - start < (cancel ? 2000u : 10000u));
        }
        if (cancel)
            assert(client.cancelled());
        else if (failure)
            assert(client.error()[0]);
        else
            assert(!client.error()[0] && client.data().state == tube5::SearchState::ready);
    }
    SDL_Quit();
}
