#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "index.hpp"

struct Suggestion {
    std::string term;
    std::size_t distance;
    std::size_t document_frequency;
};

class BkTree {
public:
    void insert(std::string term, std::size_t document_frequency);

    std::vector<Suggestion> search(std::string_view word, std::size_t max_distance,
                                   std::size_t limit) const;

    std::size_t size() const { return size_; }

private:
    struct Node {
        std::string term;
        std::size_t document_frequency;
        std::unordered_map<std::size_t, std::unique_ptr<Node>> children;
    };

    std::unique_ptr<Node> root_;
    std::size_t size_ = 0;
};

BkTree build_bk_tree(const InvertedIndex& index);

std::optional<std::string> correct_query(std::string_view query, const InvertedIndex& index,
                                         const BkTree& tree);
