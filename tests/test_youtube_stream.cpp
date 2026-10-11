// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_stream.hpp"
#include "verified_https.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
int main()
{
    std::uint64_t end = 0, total = 0;
    assert(tube5::youtube_content_range("bytes 0-524287/1000000\r\n", 0, 524288, end, total));
    assert(end == 524287 && total == 1000000);
    assert(tube5::youtube_content_range("bytes 524288-999999/1000000", 524288, 524288, end, total));
    assert(end == 999999 && total == 1000000);
    assert(tube5::youtube_content_range("bytes 0-4095/4096", 0, 524288, end, total));
    for (const auto *value :
         {"bytes 1-524288/1000000", "bytes 0-999/1000000", "bytes 0-524288/1000000",
          "bytes 0-999/999", "bytes 0-1/0", "bytes -1-5/10", "bytes 0-524287/*",
          "bytes 0-524287/1000000evil", "bytes */1000000",
          "bytes 0-524287/99999999999999999999999999", "bytes", ""})
        assert(!tube5::youtube_content_range(value, 0, 524288, end, total));
    assert(!tube5::youtube_content_range(nullptr, 0, 524288, end, total));
    assert(!tube5::youtube_content_range("bytes 0-1/10", 0, 0, end, total));
    for (const char *ip : {"0.0.0.0", "10.0.0.1", "127.0.0.1", "172.16.0.1", "192.168.8.200",
                           "169.254.1.2", "100.64.0.1", "224.0.0.1", "256.0.0.1", "invalid"})
        assert(tube5::blocked_https_ipv4(ip));
    assert(!tube5::blocked_https_ipv4("142.251.152.4"));
    tube5::YouTubeStream source;
    for (const char *url : {"http://127.0.0.1/test.mp4", "https://evil.test/videoplayback?x=1",
                            "https://a.googlevideo.com@127.0.0.1/videoplayback?x=1", ""})
    {
        assert(!source.open(url));
        assert(source.error()[0]);
        source.close();
        source.close();
    }
}
