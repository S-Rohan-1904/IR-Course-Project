#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct RankedPage {
    std::string page;
    double score;
};

std::vector<RankedPage> pagerank(const std::vector<std::pair<std::string, std::string>>& links,
                                 std::size_t iterations, double damping);
