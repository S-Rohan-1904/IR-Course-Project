#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "url.hpp"

struct FetchResult {
    int status;
    std::string content_type;
    std::string body;
};

class Fetcher {
public:
    virtual ~Fetcher() = default;
    virtual std::optional<FetchResult> get(const Url& url) = 0;
    virtual std::unique_ptr<Fetcher> clone() const = 0;
};

std::unique_ptr<Fetcher> make_file_fetcher(std::filesystem::path root);

std::unique_ptr<Fetcher> make_http_fetcher(std::string user_agent, long timeout_seconds);
