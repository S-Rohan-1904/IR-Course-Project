#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "fetcher.hpp"
#include "robots.hpp"
#include "url.hpp"

struct CrawlOptions {
    std::size_t max_pages = 50;
    std::size_t max_depth = 3;
    std::string user_agent = "searchbot";
    bool same_host_only = true;
    std::string path_prefix;
    std::size_t max_per_host = 0;
    std::size_t concurrency = 1;
    std::chrono::milliseconds min_delay{500};
    std::optional<std::filesystem::path> output_dir;
};

struct CrawledPage {
    std::string url;
    int status;
    std::string title;
    std::string text;
    std::size_t depth;
};

std::chrono::milliseconds delay_for(const RobotsRules& rules, std::chrono::milliseconds floor);

std::vector<CrawledPage> crawl(const Url& seed, Fetcher& fetcher, const CrawlOptions& options);
