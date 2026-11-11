#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

#include "url.hpp"

struct FrontierEntry {
    Url url;
    std::size_t depth;
};

class Frontier {
public:
    Frontier(std::size_t max_depth, std::size_t max_pages)
        : max_depth_(max_depth), max_pages_(max_pages) {}

    bool push(const Url& url, std::size_t depth);

    std::optional<FrontierEntry> pop();
    std::optional<FrontierEntry> pop_first(const std::function<bool(const FrontierEntry&)>& wanted);
    bool empty() const { return queue_.empty() || issued_ >= max_pages_; }

    std::size_t issued() const { return issued_; }

    std::size_t seen() const { return seen_.size(); }

private:
    std::deque<FrontierEntry> queue_;
    std::unordered_set<std::string> seen_;
    std::size_t max_depth_;
    std::size_t max_pages_;
    std::size_t issued_ = 0;
};
