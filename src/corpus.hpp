#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "document.hpp"

std::vector<std::string> list_document_ids(const std::filesystem::path& corpus_dir);

std::uint64_t fingerprint_file(const std::filesystem::path& path);

class CorpusReader {
public:
    virtual ~CorpusReader() = default;

    virtual const std::vector<std::string>& document_ids() const = 0;

    virtual std::optional<Document> read(const std::string& id) const = 0;
    virtual std::uint64_t fingerprint(const std::string& id) const = 0;
};

std::unique_ptr<CorpusReader> open_corpus(const std::filesystem::path& source,
                                          std::string& error);
