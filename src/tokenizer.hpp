#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct Token {
    std::string text;
    std::size_t position;
    std::size_t offset;
};

std::vector<Token> tokenize(std::string_view text);
