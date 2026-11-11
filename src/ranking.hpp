#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "index.hpp"

struct ScoredDocument {
    std::size_t doc_id;
    double score;
};

std::vector<std::string> query_terms(const std::vector<std::string>& words);

std::vector<ScoredDocument> score_tfidf(const std::vector<std::string>& terms,
                                        const InvertedIndex& index);

std::vector<ScoredDocument> score_bm25(const std::vector<std::string>& terms,
                                       const InvertedIndex& index);

enum class Scorer {
    TfIdf,
    Bm25,
    Bm25F,
};

std::optional<Scorer> parse_scorer(std::string_view name);

const char* scorer_name(Scorer scorer);

std::vector<ScoredDocument> score(const std::vector<std::string>& terms,
                                  const InvertedIndex& index, Scorer scorer,
                                  std::size_t threads);

std::vector<ScoredDocument> score_unordered(const std::vector<std::string>& terms,
                                            const InvertedIndex& index, Scorer scorer,
                                            std::size_t threads);

std::vector<ScoredDocument> top_k(const std::vector<ScoredDocument>& scored, std::size_t k);
