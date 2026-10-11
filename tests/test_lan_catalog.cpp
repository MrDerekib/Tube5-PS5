// SPDX-License-Identifier: GPL-3.0-or-later
#include "lan_catalog.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main(int argc, char **argv)
{
    tube5::CatalogData data;
    const std::string valid =
        "TUBE5\t1\nnetwork-test\tVIDEO 1080\tqf31PkeESoU\t600\t1080\nlocal-test\tLOCAL\t-\t20\t0\n";
    assert(tube5::parse_lan_catalog(valid, data) && data.count == 2);
    assert(data.entries[0].seconds == 600);
    assert(std::strcmp(data.entries[0].youtube_id.data(), "qf31PkeESoU") == 0);
    for (const auto &invalid :
         {std::string{}, std::string("TUBE5\t2\n"), valid.substr(0, valid.size() - 1),
          valid + "local-test\tDUPLICATE\t-\t1\t0\n", std::string(4097, 'x'),
          std::string("TUBE5\t1\n../private\tX\t-\t1\t0\n"),
          std::string("TUBE5\t1\na\tX\t-\t99999999999999\t1080\n"),
          std::string("TUBE5\t1\na\tX\t-\t600\t2160\n"),
          std::string("TUBE5\t1\na\tX\tbad-id\t1\t0\n")})
    {
        assert(!tube5::parse_lan_catalog(invalid, data));
        assert(data.count == 0);
    }
    std::string many = "TUBE5\t1\n";
    for (int i = 0; i < 9; ++i)
        many += "id" + std::to_string(i) + "\tX\t-\t1\t0\n";
    assert(!tube5::parse_lan_catalog(many, data));
    char base[64]{};
    assert(tube5::lan_catalog_base("http://192.168.8.176:8080/test-clip.mp4", base, sizeof(base)));
    assert(std::strcmp(base, "http://192.168.8.176:8080") == 0);
    for (const char *url : {"https://192.168.8.176:8080/x", "http://example.com/x",
                            "http://user@127.0.0.1/x", "http://127.0.0.1:0/x",
                            "http://127.0.0.1:65536/x", "http://256.0.0.1/x", "http://127.0.1/x"})
        assert(!tube5::lan_catalog_base(url, base, sizeof(base)));
    if (argc > 1)
    {
        assert(SDL_Init(SDL_INIT_TIMER) == 0);
        tube5::LanCatalog catalog;
        const Uint64 start = SDL_GetTicks64();
        assert(catalog.begin(argv[1]));
        assert(SDL_GetTicks64() - start < 250);
        const bool cancel = argc > 2 && std::strcmp(argv[2], "--cancel") == 0;
        const bool failure = argc > 2 && std::strcmp(argv[2], "--failure") == 0;
        if (cancel)
        {
            SDL_Delay(30);
            catalog.cancel();
        }
        while (catalog.loading())
        {
            catalog.poll();
            assert(SDL_GetTicks64() - start < (cancel ? 2000 : 10000));
            SDL_Delay(5);
        }
        if (cancel || failure)
            assert(catalog.data().count == 0);
        else
        {
            assert(!catalog.error()[0]);
            assert(catalog.data().count == 2);
            char url[128]{};
            assert(catalog.media_url(0, url, sizeof(url)));
            assert(std::string_view{url}.ends_with("/media/network-test.mp4"));
            assert(!catalog.media_url(8, url, sizeof(url)));
            assert(!catalog.begin("https://example.com"));
            assert(catalog.data().count == 0);
        }
        if (failure)
            assert(catalog.error()[0]);
        SDL_Quit();
    }
}
