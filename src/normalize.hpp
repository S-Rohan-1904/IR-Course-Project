#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "tokenizer.hpp"

std::string normalize(std::string_view token);

std::vector<Token> normalize_tokens(std::vector<Token> tokens);
