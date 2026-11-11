#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>

struct WikiImportReport {
    std::size_t lines_seen = 0;
    std::size_t documents_written = 0;
    std::size_t skipped_empty = 0;
};

enum class WikiField {
    Text,
    OpeningText,
};

bool import_cirrussearch(std::istream& in, const std::filesystem::path& output,
                         WikiField field, std::size_t limit, WikiImportReport& report,
                         std::string& error);
