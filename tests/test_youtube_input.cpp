// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_input.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main()
{
    std::array<char, 12> id{};
    for (const auto *input :
         {"bHIhgxav9LY", " bHIhgxav9LY ", "https://youtu.be/bHIhgxav9LY?si=x",
          "https://www.youtube.com/watch?v=bHIhgxav9LY&t=4",
          "https://youtube.com/watch?si=x&v=bHIhgxav9LY#time",
          "http://m.youtube.com/watch?v=bHIhgxav9LY", "https://www.youtube.com/shorts/bHIhgxav9LY",
          "https://youtube.com/live/bHIhgxav9LY", "https://music.youtube.com/watch?v=bHIhgxav9LY",
          "https://youtube.com/embed/bHIhgxav9LY"})
    {
        assert(tube5::youtube_input_id(input, id));
        assert(std::strcmp(id.data(), "bHIhgxav9LY") == 0);
    }
    assert(tube5::youtube_input_id("qf31PkeESoU", id));
    assert(std::strcmp(id.data(), "qf31PkeESoU") == 0);
    for (const auto *bad :
         {"", "bHIhgxav9L", "bHIhgxav9LYx", "bHIhgxav9L!", "bHIhgxav9L\n",
          "https://youtube.com.evil.test/watch?v=bHIhgxav9LY",
          "https://youtube.com@localhost/watch?v=bHIhgxav9LY",
          "https://127.0.0.1/watch?v=bHIhgxav9LY", "https://youtube.com:443/watch?v=bHIhgxav9LY",
          "https://youtube.com/watch?v=bHIhgxav9LY&v=qf31PkeESoU",
          "https://youtube.com/watch?v=%62HIhgxav9LY", "https://youtu.be/bHIhgxav9LY/extra",
          "https://youtube.com/playlist?list=x", "file:///etc/passwd"})
    {
        assert(!tube5::youtube_input_id(bad, id));
        assert(!id[0]);
    }
    assert(!tube5::youtube_input_id(std::string(1025, 'a'), id));
    tube5::VideoKeyboard keyboard;
    static_assert(tube5::VideoKeyboard::keys.size() == 72);
    keyboard.move(-1);
    assert(keyboard.selected() == 71);
    keyboard.move(1);
    assert(keyboard.selected() == 0);
    keyboard.choose();
    keyboard.move(26);
    keyboard.choose();
    assert(std::strcmp(keyboard.input(), "Aa") == 0);
    keyboard.erase();
    assert(std::strcmp(keyboard.input(), "A") == 0);
    assert(keyboard.set("https://youtu.be/qf31PkeESoU?si=x"));
    assert(tube5::youtube_input_id(keyboard.input(), id));
    assert(!keyboard.set("invalid\n"));
    assert(!keyboard.set(std::string(1025, 'a')));
    keyboard.clear();
    for (unsigned i = 0; i < 1200; ++i)
        keyboard.append('a');
    assert(std::strlen(keyboard.input()) == 1024);
    keyboard.erase();
    assert(std::strlen(keyboard.input()) == 1023);
}
