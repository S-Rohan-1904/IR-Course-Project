#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "corpus.hpp"
#include "tokenizer.hpp"

class MappedIndex;

struct Posting {
    std::size_t doc_id;
    std::size_t frequency;
    std::vector<std::size_t> positions;
};

struct TitleField {
    std::size_t span = 0;
    std::size_t terms = 0;
};

class InvertedIndex {
public:
    void add_document(std::string doc_id, const std::vector<Token>& terms,
                      std::uint64_t fingerprint = 0, std::size_t title_span = 0);

    std::size_t document_count() const;

    std::size_t term_count() const;

    std::size_t posting_count() const;

    std::size_t document_frequency(std::string_view term) const;

    const std::vector<Posting>& postings(std::string_view term) const;

    std::size_t document_length(std::size_t doc_id) const;

    double average_document_length() const;

    TitleField document_title(std::size_t doc_id) const;

    double average_title_length() const;

    std::uint64_t document_fingerprint(std::size_t doc_id) const;

    std::optional<std::size_t> find_document(std::string_view doc_id) const;

    std::vector<Token> document_terms(std::size_t doc_id) const;

    const std::vector<std::string>& document_ids() const;

    std::vector<std::string> terms() const;

    void append(InvertedIndex other);

    static InvertedIndex from_mapped(MappedIndex mapped);

    static InvertedIndex from_parts(
        std::vector<std::string> document_ids,
        std::vector<std::size_t> document_lengths,
        std::vector<std::uint64_t> document_fingerprints,
        std::unordered_map<std::string, std::vector<Posting>> postings,
        std::vector<TitleField> document_titles = {});

private:
    struct Backing;
    std::shared_ptr<Backing> mapped_;

    mutable std::vector<std::string> document_ids_;
    std::vector<std::size_t> document_lengths_;
    std::vector<std::uint64_t> document_fingerprints_;
    std::vector<TitleField> document_titles_;
    std::unordered_map<std::string, std::vector<Posting>> postings_;

    std::size_t total_document_length_ = 0;
    std::size_t total_title_length_ = 0;
};

InvertedIndex build_index_from(const CorpusReader& corpus, const std::vector<std::string>& ids);

InvertedIndex build_index(const std::filesystem::path& corpus_dir);

struct IndexUpdateReport {
    std::size_t added = 0;
    std::size_t updated = 0;
    std::size_t removed = 0;
    std::size_t unchanged = 0;
};

InvertedIndex update_index(const InvertedIndex& previous,
                           const std::filesystem::path& corpus_dir, IndexUpdateReport& report);

struct BuildProfile {
    double open_ms = 0.0;
    double index_ms = 0.0;
    double merge_ms = 0.0;
};

InvertedIndex build_index_parallel(const std::filesystem::path& corpus_dir,
                                   std::size_t threads);

InvertedIndex build_index_parallel(const std::filesystem::path& corpus_dir, std::size_t threads,
                                   BuildProfile& profile);
