#pragma once

#include <cstddef>
#include <string_view>

std::size_t edit_distance(std::string_view a, std::string_view b);

std::size_t bounded_edit_distance(std::string_view a, std::string_view b, std::size_t limit);
