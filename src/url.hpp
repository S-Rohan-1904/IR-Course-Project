#pragma once

#include <optional>
#include <string>
#include <string_view>

struct Url {
    std::string scheme;
    std::string host;
    int port;
    std::string path;
    std::string query;
};

std::optional<Url> parse_url(std::string_view text);

std::optional<Url> resolve_url(const Url& base, std::string_view reference);

std::string url_to_string(const Url& url);
