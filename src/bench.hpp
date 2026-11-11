#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include "index.hpp"

struct Measurement {
    std::string name;
    std::string value;
    bool stable;
};

std::uint64_t peak_memory_bytes();

std::uint64_t source_bytes(const std::filesystem::path& source);

std::vector<Measurement> bench_build(const std::filesystem::path& source, std::size_t threads);

std::vector<Measurement> bench_query(const InvertedIndex& index, const CorpusReader* corpus,
                                     const std::vector<std::string>& queries, std::size_t limit,
                                     std::size_t repeats, bool snippets);

void print_measurements(std::ostream& out, const std::vector<Measurement>& measurements,
                        bool stable_only);

std::vector<Measurement> bench_environment(const std::filesystem::path& source);
