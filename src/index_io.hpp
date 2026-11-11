#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "index.hpp"

enum class IndexEncoding {
    Plain,
    Delta,
    VarByte,
};

std::vector<unsigned char> encode_index(const InvertedIndex& index, IndexEncoding encoding);

struct IndexSizeReport {
    std::size_t total;
    std::size_t header;
    std::size_t documents;
    std::size_t dictionary;
    std::size_t postings;
    std::size_t positions;
};

IndexSizeReport measure_index(const InvertedIndex& index, IndexEncoding encoding);

std::optional<InvertedIndex> decode_index(const std::vector<unsigned char>& bytes,
                                          std::string& error);

bool write_index_file(const InvertedIndex& index, const std::filesystem::path& path,
                      IndexEncoding encoding, std::string& error);
std::optional<InvertedIndex> read_index_file(const std::filesystem::path& path,
                                             std::string& error);

std::optional<IndexEncoding> parse_encoding(std::string_view name);
