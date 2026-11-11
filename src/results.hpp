#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "corpus.hpp"
#include "index.hpp"
#include "prior.hpp"
#include "ranking.hpp"

struct QueryOptions {
    std::size_t limit = 10;
    Scorer scorer = Scorer::Bm25;
    bool snippets = false;
    std::size_t snippet_chars = 160;
    bool tsv = false;
    double pagerank_weight = 0.0;
    LinkGraph pagerank_graph = LinkGraph::Page;

    std::size_t pagerank_max_words = 0;

    std::string corpus;
};

struct QueryResult {
    std::string document_id;
    double score;
    std::string snippet;
};

std::optional<std::vector<QueryResult>> run_query(
    const InvertedIndex& index, const CorpusReader* corpus, std::string_view query,
    const QueryOptions& options, std::string& error, const PageRankPrior* prior = nullptr);

void print_results(std::ostream& out, const std::vector<QueryResult>& results,
                   const QueryOptions& options);
