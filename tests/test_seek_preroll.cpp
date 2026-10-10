// SPDX-License-Identifier: GPL-3.0-or-later
#include "seek_preroll.hpp"
#include <cassert>

int main()
{
    using namespace tube5::seek_preroll;
    AVFormatContext format{};
    AVCodecContext codec{};
    AVFrame frame{};
    codec.codec_type = AVMEDIA_TYPE_VIDEO;
    codec.pkt_timebase = {1, 30};
    frame.best_effort_timestamp = 0;
    frame.pts = AV_NOPTS_VALUE;
    reset();
    assert(!discard(&codec, &frame));
    set(&format, -1, 5000000, 0);
    assert(discard(&codec, &frame));
    frame.best_effort_timestamp = 149;
    assert(discard(&codec, &frame));
    frame.best_effort_timestamp = 150;
    assert(!discard(&codec, &frame));
    frame.best_effort_timestamp = 180;
    assert(!discard(&codec, &frame));
    set(&format, -1, 1000000, 0);
    frame.best_effort_timestamp = 30;
    assert(!discard(&codec, &frame));
    codec.codec_type = AVMEDIA_TYPE_AUDIO;
    codec.pkt_timebase = {1, 48000};
    frame.best_effort_timestamp = 47999;
    assert(discard(&codec, &frame));
    frame.best_effort_timestamp = AV_NOPTS_VALUE;
    frame.pts = 48000;
    assert(!discard(&codec, &frame));
    frame.pts = AV_NOPTS_VALUE;
    assert(!discard(&codec, &frame));
    AVStream stream{};
    stream.time_base = {1, 1000};
    AVStream *streams[] = {&stream};
    format.streams = streams;
    format.nb_streams = 1;
    set(&format, 0, 2000, 0);
    assert(target_us == 2000000);
    set(&format, 1, 50, 0);
    assert(target_us == 2000000);
    set(&format, -1, 50, AVSEEK_FLAG_BYTE);
    assert(target_us == 2000000);
    codec.pkt_timebase = {0, 0};
    frame.pts = 0;
    assert(!discard(&codec, &frame));
    reset();
    assert(target_us == AV_NOPTS_VALUE);
}
