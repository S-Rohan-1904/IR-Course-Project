#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "index.hpp"
#include "results.hpp"

// Evaluating ranking against a collection someone else judged.
//
// Precision measured against relevance judgements written for the engine being
// measured is not evidence of anything. BEIR is a public benchmark: a corpus, a
// query set, and qrels produced independently, with BM25 baselines published
// per dataset. Scoring against those makes the number checkable by someone who
// does not trust this code, and comparable to a reference implementation.
//
// The layout on disk is BEIR's own:
//
//     corpus.jsonl     {"_id": ..., "title": ..., "text": ...}
//     queries.jsonl    {"_id": ..., "text": ...}
//     qrels/test.tsv   query-id <tab> corpus-id <tab> score, with a header row

struct BeirImportReport {
    std::size_t lines_seen = 0;
    std::size_t documents_written = 0;
    std::size_t skipped_empty = 0;
};

// Converts a BEIR corpus.jsonl into a .corpus file.
//
// Document ids are BEIR's own, not ordinals, because the qrels name them. The
// corpus format requires ids in sorted order, so the ids are read, sorted, and
// the documents written in that order; a BEIR corpus is small enough for that
// to be reasonable.
bool import_beir(const std::filesystem::path& corpus_jsonl, const std::filesystem::path& output,
                 BeirImportReport& report, std::string& error);

// One query and the documents judged relevant to it.
struct JudgedQuery {
    std::string id;
    std::string text;
};

struct RelevanceReport {
    std::size_t queries = 0;      // queries with at least one judged document
    std::size_t unjudged = 0;     // queries in the file with no qrels
    std::size_t cutoff = 10;      // the k every at_k measure below was taken at
    double precision_at_k = 0.0;
    double recall_at_k = 0.0;
    double f1_at_k = 0.0;
    double ndcg_at_k = 0.0;
    double mrr_at_k = 0.0;
    double mean_average_precision = 0.0;
    double r_precision = 0.0;
    double recall_at_100 = 0.0;

    // The highest precision_at_k any ranking could reach on these judgements:
    // a query with fewer than k relevant documents cannot fill k slots, so on a
    // collection like SciFact, where most queries have one relevant document,
    // precision_at_10 is capped near 0.11 however good the ranking is. Reported
    // so that precision_at_k can be read against what is achievable.
    double max_precision_at_k = 0.0;
};

// Runs every judged query and scores the ranking against the qrels.
//
// nDCG@k with graded gains, which is what BEIR reports, so the number can be
// put next to the published baseline. The rest are the measures a reader will
// ask for: precision, recall and their harmonic mean at the same k, mean
// reciprocal rank, mean average precision, and R-precision.
//
// `cutoff` is the k. Enough results are retrieved for every measure to be
// exact: at least 100 for recall_at_100, at least k, and at least as many as
// the query has relevant documents, which is what R-precision needs.
bool evaluate_relevance(const InvertedIndex& index, const std::filesystem::path& queries_jsonl,
                        const std::filesystem::path& qrels_tsv, const QueryOptions& options,
                        std::size_t cutoff, RelevanceReport& report, std::string& error);
