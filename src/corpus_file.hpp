#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "document.hpp"
#include "mapped_file.hpp"

class CorpusFile {
public:
    static std::optional<CorpusFile> open(const std::filesystem::path& path, std::string& error);

    const std::vector<std::string>& document_ids() const { return ids_; }

    std::optional<Document> read(const std::string& id) const;

    std::uint64_t fingerprint(const std::string& id) const;

    std::uint64_t bytes() const { return static_cast<std::uint64_t>(data_.size()); }

    CorpusFile(CorpusFile&&) noexcept = default;
    CorpusFile& operator=(CorpusFile&&) noexcept = default;
    CorpusFile(const CorpusFile&) = delete;
    CorpusFile& operator=(const CorpusFile&) = delete;
    CorpusFile() = default;

private:
    struct Entry {
        std::size_t offset;
        std::size_t length;
    };

    std::size_t position_of(const std::string& id) const;

    MappedFile data_;
    std::vector<std::string> ids_;
    std::vector<Entry> entries_;
    std::vector<std::size_t> order_;
};

class CorpusWriter {
public:
    static std::optional<CorpusWriter> create(const std::filesystem::path& output,
                                              std::string& error);

    CorpusWriter() = default;
    CorpusWriter(CorpusWriter&&) = default;
    CorpusWriter& operator=(CorpusWriter&&) = default;

    bool add(const std::string& id, std::string_view raw);

    bool finish(std::string& error);

    std::size_t count() const { return ids_.size(); }

private:
    struct Placed {
        std::size_t offset;
        std::size_t length;
    };

    std::ofstream out_;
    std::vector<std::string> ids_;
    std::vector<Placed> placed_;
    std::size_t cursor_ = 0;
};

bool write_corpus_file(const std::filesystem::path& corpus_dir,
                       const std::filesystem::path& output, std::string& error);
