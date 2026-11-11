#pragma once

#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> field_of(std::string_view line, std::string_view key);

std::string collapse(std::string_view text);
