#pragma once

#include <string_view>
#include <vector>

#include "document.hpp"
#include "tokenizer.hpp"

std::vector<Token> stem_tokens(std::vector<Token> tokens);

std::vector<Token> split_compounds(std::vector<Token> tokens);

std::vector<Token> analyze(std::string_view text);

std::vector<Token> analyze_document(const Document& doc);

std::size_t title_span(const Document& doc);
