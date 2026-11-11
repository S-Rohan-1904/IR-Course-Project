#pragma once

#include <string_view>
#include <vector>

#include "tokenizer.hpp"

bool is_stopword(std::string_view term);

std::vector<Token> remove_stopwords(std::vector<Token> tokens);
