#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "index.hpp"

struct Completion {
    std::string term;
    std::size_t document_frequency;
};

class Trie {
public:
    void insert(std::string_view term, std::size_t document_frequency);

    std::vector<Completion> complete(std::string_view prefix, std::size_t limit) const;

    std::size_t size() const { return size_; }

private:
    struct Node {
        std::unordered_map<char, std::unique_ptr<Node>> children;
        std::size_t document_frequency = 0;
        bool terminal = false;
    };

    static void collect(const Node& node, std::string& prefix, std::vector<Completion>& out);

    Node root_;
    std::size_t size_ = 0;
};

Trie build_trie(const InvertedIndex& index);
