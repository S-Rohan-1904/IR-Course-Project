#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct ExternalBuildReport {
    std::size_t documents = 0;
    std::size_t terms = 0;
    std::size_t postings = 0;
    std::size_t blocks = 0;
    double index_ms = 0.0;
    double merge_ms = 0.0;
};

bool build_index_external(const std::filesystem::path& source,
                          const std::filesystem::path& output, std::size_t block_documents,
                          std::size_t threads, ExternalBuildReport& report, std::string& error);
