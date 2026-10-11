// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube_visitor.hpp"
#include <cassert>
#include <cstring>
#include <string>
int main()
{
    std::array<char, 4097> visitor{};
    assert(
        tube5::youtube_visitor(R"({"responseContext":{"visitorData":"abc_-123%3D="}})", visitor));
    assert(std::strcmp(visitor.data(), "abc_-123%3D=") == 0);
    for (const auto &bad :
         {std::string("{}"), std::string("[]"),
          std::string(R"({"responseContext":{"visitorData":""}})"),
          std::string(R"({"responseContext":{"visitorData":123}})"),
          std::string(R"({"responseContext":{"visitorData":"x\"inject"}})"),
          std::string(R"({"responseContext":{"visitorData":"a"}} trailing)"),
          std::string(R"({"responseContext":{"visitorData":")") + std::string(4097, 'a') + "\"}}"})
    {
        assert(!tube5::youtube_visitor(bad, visitor));
        assert(!visitor[0]);
    }
}
