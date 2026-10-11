// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_probe.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
int main(int argc, char **argv)
{
    std::array<char, 128> title;
    assert(tube5::probe_title("{\"title\":\"Electricity\",\"author_name\":\"Veritasium\"}", title));
    assert(std::strcmp(title.data(), "Electricity") == 0);
    assert(tube5::probe_title("{\"title\" : \"Quoted \\\"Text\\\" \\u00e1\",\"author_name\":\"X\"}",
                              title));
    assert(std::strcmp(title.data(), "Quoted \"Text\" ?") == 0);
    for (const auto &bad :
         {std::string{}, std::string(16385, 'x'), std::string("{\"title\":\"no author\"}"),
          std::string("{\"title\":123,\"author_name\":\"X\"}"),
          std::string("{\"title\":\"bad\\z\",\"author_name\":\"X\"}"),
          std::string("{\"title\":\"bad\\u123Z\",\"author_name\":\"X\"}"),
          std::string("{\"author_name\":\"X\",\"title\":\"unterminated")})
        assert(!tube5::probe_title(bad, title));
    if (argc < 2)
        return 0;
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    {
        tube5::YouTubeProbe probe;
        const auto started = SDL_GetTicks64();
        assert(probe.begin());
        assert(SDL_GetTicks64() - started < 250);
        if (std::strcmp(argv[1], "--cancel") == 0)
            probe.cancel();
        while (probe.loading())
        {
            probe.poll();
            SDL_Delay(10);
            assert(SDL_GetTicks64() - started < 16000);
        }
        const auto &result = probe.result();
        std::printf("DNS %d IP %s HTTP %ld CURL %d CERT %ld BYTES %u TITLE %s ERROR %s\n",
                    result.dns_error, result.ip.data(), result.http, result.curl_error,
                    result.verify, result.bytes, result.title.data(), result.error.data());
        if (std::strcmp(argv[1], "--cancel") == 0)
            assert(probe.stage() == tube5::ProbeStage::cancelled);
        else
            assert(probe.stage() == tube5::ProbeStage::ready && result.http == 200 &&
                   result.verify == 0);
    }
    SDL_Quit();
}
