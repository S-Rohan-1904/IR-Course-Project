#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct SnippetOptions {
    std::size_t max_chars = 200;
    std::string open = "[";
    std::string close = "]";
};

std::string make_snippet(std::string_view text, const std::vector<std::string>& terms,
                         const SnippetOptions& options);
