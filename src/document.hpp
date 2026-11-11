#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct Document {
    std::string id;
    std::string title;
    std::string body;
};

std::optional<Document> parse_document(std::string id, std::string_view raw);

std::optional<Document> read_document(const std::filesystem::path& corpus_dir,
                                      const std::string& id);
