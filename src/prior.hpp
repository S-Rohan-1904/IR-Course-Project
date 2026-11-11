#pragma once

#include <cstddef>
#include <filesystem>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "index.hpp"
#include "pagerank.hpp"

enum class LinkGraph { Page, Site, Home };

std::optional<LinkGraph> parse_link_graph(std::string_view text);
const char* link_graph_name(LinkGraph graph);

class PageRankPrior {
public:
    PageRankPrior(const std::vector<RankedPage>& ranking, const InvertedIndex& index);

    static std::optional<PageRankPrior> load(const std::filesystem::path& path,
                                             const InvertedIndex& index, std::string& error);

    double boost(std::size_t doc_id, double weight) const;

    std::size_t covered() const;

private:
    std::vector<double> relative_;
    std::size_t covered_ = 0;
};

class PriorSet {
public:
    void add(LinkGraph graph, PageRankPrior prior);

    const PageRankPrior* find(LinkGraph graph) const;

private:
    std::array<std::optional<PageRankPrior>, 3> priors_;
};

bool read_links(const std::filesystem::path& path,
                std::vector<std::pair<std::string, std::string>>& links, std::string& error);

bool write_pagerank_file(const std::filesystem::path& path,
                         const std::vector<RankedPage>& ranking, std::string& error);

std::optional<std::vector<RankedPage>> read_pagerank_file(const std::filesystem::path& path,
                                                          std::string& error);
