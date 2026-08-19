#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mojib_text {
struct Mora {
    std::vector<char> phones;
    int weight = 1;
    std::uint8_t source_code = 0;
};

std::vector<Mora> analyze(std::string_view utf8);
}
