#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "index.hpp"
#include "index_io.hpp"
#include "mapped_file.hpp"

std::uint64_t pack_title(const TitleField& title);

TitleField unpack_title(std::uint64_t packed);

class MappedIndex {
public:
    static std::optional<MappedIndex> open(const std::filesystem::path& path, std::string& error);

    std::size_t document_count() const { return document_count_; }
    std::size_t term_count() const { return term_count_; }
    std::size_t posting_count() const { return posting_count_; }

    std::size_t document_length(std::size_t doc_id) const;
    std::uint64_t document_fingerprint(std::size_t doc_id) const;
    TitleField document_title(std::size_t doc_id) const;
    std::string_view document_id(std::size_t doc_id) const;

    std::vector<Posting> postings(std::string_view term) const;

    std::vector<Token> document_terms(std::size_t doc_id) const;

    std::vector<std::string> terms() const;

    std::string_view term_at(std::size_t slot) const;
    std::vector<Posting> postings_at(std::size_t slot) const;

    std::uint64_t total_document_length() const { return total_document_length_; }
    std::uint64_t total_title_length() const { return total_title_length_; }

private:
    MappedFile data_;
    IndexEncoding encoding_ = IndexEncoding::VarByte;
    std::size_t document_count_ = 0;
    std::size_t term_count_ = 0;
    std::size_t posting_count_ = 0;
    std::size_t lengths_at_ = 0;
    std::size_t fingerprints_at_ = 0;
    std::size_t titles_at_ = 0;
    std::size_t id_offsets_at_ = 0;
    std::size_t term_offsets_at_ = 0;
    std::uint64_t total_document_length_ = 0;
    std::uint64_t total_title_length_ = 0;
};

std::vector<unsigned char> encode_index_v3(const InvertedIndex& index, IndexEncoding encoding,
                                           IndexSizeReport* report);
