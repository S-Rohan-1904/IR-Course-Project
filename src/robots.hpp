#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct RobotsRule {
    std::string pattern;
    bool allow;
};

class RobotsRules {
public:
    static RobotsRules parse(std::string_view text, std::string_view user_agent);

    bool allows(std::string_view path) const;

    std::optional<std::chrono::milliseconds> crawl_delay() const { return crawl_delay_; }

    const std::vector<RobotsRule>& rules() const { return rules_; }

private:
    std::vector<RobotsRule> rules_;
    std::optional<std::chrono::milliseconds> crawl_delay_;
};
