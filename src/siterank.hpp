#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "pagerank.hpp"

std::vector<RankedPage> site_pagerank(const std::vector<std::pair<std::string, std::string>>& links,
                                      const std::vector<std::pair<std::string, std::string>>& urls,
                                      std::size_t iterations, double damping);

std::vector<RankedPage> home_page_ranking(
    const std::vector<std::pair<std::string, std::string>>& urls);

bool read_page_urls(const std::filesystem::path& path,
                    std::vector<std::pair<std::string, std::string>>& urls, std::string& error);
