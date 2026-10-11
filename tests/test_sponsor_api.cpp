// SPDX-License-Identifier: GPL-3.0-or-later
#include "sponsor_api.hpp"
#include <cassert>
#include <cstdio>
#include <string>
int main(int argc, char **)
{
    using namespace tube5;
    SponsorResponse response;
    constexpr auto id = "bHIhgxav9LY";
    const std::string prefix = R"([{"videoID":"bHIhgxav9LY","segments":[)";
    const std::string segment =
        R"({"category":"sponsor","actionType":"skip","segment":[0,4],"videoDuration":887,"votes":1})";
    assert(parse_sponsor_response(prefix + segment + "]}]", id, response));
    assert(response.data.state == SponsorState::ready && response.data.count == 1);
    assert(aligned_sponsor(response, 887).ranges[0].end_ms == 4000);
    assert(aligned_sponsor(response, 880).state == SponsorState::unaligned);
    assert(aligned_sponsor(response, 0).state == SponsorState::unaligned);
    assert(parse_sponsor_response("[]", id, response) && response.data.state == SponsorState::none);
    assert(parse_sponsor_response(R"([{"videoID":"abcdefghijk","segments":[]}])", id, response));
    assert(response.data.count == 0);
    assert(!parse_sponsor_response(prefix + segment + "]}]{}", id, response));
    assert(!parse_sponsor_response(
        prefix + R"({"category":"sponsor","actionType":"skip","segment":[4,0]})]}])", id,
        response));
    assert(!parse_sponsor_response(
        prefix + R"({"category":"sponsor","actionType":"skip","segment":[true,4]})]}])", id,
        response));
    assert(!parse_sponsor_response("{}", id, response));
    assert(!parse_sponsor_response(std::string(131073, ' '), id, response));
    assert(!parse_sponsor_response("[]", "bad", response));
    assert(parse_sponsor_response(prefix + segment + "," + segment + "]}]", id, response));
    assert(response.data.count == 1); // merge overlaps
    auto mismatch = segment;
    mismatch.replace(mismatch.find("887"), 3, "800");
    assert(parse_sponsor_response(prefix + segment + "," + mismatch + "]}]", id, response));
    assert(response.data.state == SponsorState::unaligned);
    auto ignored = segment;
    ignored.replace(ignored.find("sponsor"), 7, "intro");
    assert(parse_sponsor_response(prefix + ignored + "]}]", id, response));
    assert(response.data.state == SponsorState::none);
    std::string many = prefix;
    for (unsigned i = 0; i < 9; ++i)
    {
        if (i)
            many += ',';
        many += "{\"category\":\"sponsor\",\"actionType\":\"skip\",\"segment\":[" +
                std::to_string(i * 10) + ',' + std::to_string(i * 10 + 4) + "]}";
    }
    assert(!parse_sponsor_response(many + "]}]", id, response));
    std::atomic<bool> cancel{true};
    assert(fetch_sponsor(id, cancel).data.state == SponsorState::unavailable);
    if (argc > 1)
    {
        cancel = false;
        response = fetch_sponsor(id, cancel);
        std::printf("SB HTTP %ld CURL %d CERT %ld SEG %u DURATION %.3f ERROR %s\n", response.http,
                    response.curl_error, response.verify, response.data.count,
                    response.source_duration, response.error.data());
        assert(response.http == 200 && response.curl_error == 0 && response.verify == 0);
        assert(response.data.state == SponsorState::ready);
        for (unsigned i = 0; i < response.data.count; ++i)
            std::printf("SB RANGE %u %u\n", response.data.ranges[i].start_ms,
                        response.data.ranges[i].end_ms);
    }
}
