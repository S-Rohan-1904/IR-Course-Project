#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "index.hpp"
#include "results.hpp"

struct BeirImportReport {
    std::size_t lines_seen = 0;
    std::size_t documents_written = 0;
    std::size_t skipped_empty = 0;
};

bool import_beir(const std::filesystem::path& corpus_jsonl, const std::filesystem::path& output,
                 BeirImportReport& report, std::string& error);

struct JudgedQuery {
    std::string id;
    std::string text;
};

struct RelevanceReport {
    std::size_t queries = 0;
    std::size_t unjudged = 0;
    std::size_t cutoff = 10;
    double precision_at_k = 0.0;
    double recall_at_k = 0.0;
    double f1_at_k = 0.0;
    double ndcg_at_k = 0.0;
    double mrr_at_k = 0.0;
    double mean_average_precision = 0.0;
    double r_precision = 0.0;
    double recall_at_100 = 0.0;

    double max_precision_at_k = 0.0;
};

bool evaluate_relevance(const InvertedIndex& index, const std::filesystem::path& queries_jsonl,
                        const std::filesystem::path& qrels_tsv, const QueryOptions& options,
                        std::size_t cutoff, RelevanceReport& report, std::string& error);
