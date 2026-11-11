#pragma once

#include <cstddef>
#include <vector>

#include "index.hpp"
#include "query.hpp"

std::vector<std::size_t> intersect(const std::vector<std::size_t>& a,
                                   const std::vector<std::size_t>& b);
std::vector<std::size_t> unite(const std::vector<std::size_t>& a,
                               const std::vector<std::size_t>& b);
std::vector<std::size_t> subtract(const std::vector<std::size_t>& a,
                                  const std::vector<std::size_t>& b);

std::vector<std::size_t> all_documents(const InvertedIndex& index);

std::vector<std::size_t> evaluate(const QueryNode& node, const InvertedIndex& index);
