#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "analyzer.hpp"
#include "beir.hpp"
#include "bench.hpp"
#include "corpus.hpp"
#include "corpus_file.hpp"
#include "crawler.hpp"
#include "fetcher.hpp"
#include "evaluator.hpp"
#include "index.hpp"
#include "index_io.hpp"
#include "pagerank.hpp"
#include "query.hpp"
#include "ranking.hpp"
#include "results.hpp"
#include "url.hpp"
#include "wiki_import.hpp"

namespace {

int usage() {
    std::cerr << "usage: search <command> [args...]\n"
                 "\n"
                 "preprocessing:\n"
                 "  wiki-import [--limit <n>] [--abstracts] <dump.json|-> <file>\n"
                 "                               build a corpus from a Wikipedia CirrusSearch dump\n"
                 "  beir-import <corpus.jsonl> <file>\n"
                 "                               build a corpus from a BEIR dataset\n"
                 "  corpus-write <corpus_dir> <file>\n"
                 "                               pack a corpus directory into one file\n"
                 "  crawl [options] <seed_url>   crawl from a seed url\n"
                 "      --delay <ms>             floor between requests to one host\n"
                 "      --mirror <dir>           fetch from a mirrored site on disk\n"
                 "      --out <dir>              write the pages as a corpus\n"
                 "      --max-pages <n>          stop after n pages\n"
                 "      --max-depth <n>          follow links at most n deep\n"
                 "      --user-agent <name>      the name robots.txt is matched against\n"
                 "      --all-hosts              follow links off the seed host\n"
                 "  analyze <text>               show the terms the analysis chain produces\n"
                 "\n"
                 "indexing:\n"
                 "  index-write [--encoding <name>] [--threads <n>] <source> <file>\n"
                 "                               save an index to disk\n"
                 "  index-size <source>          encoded size under each encoding\n"
                 "  pagerank [--iterations <n>] [--damping <d>] <links_file>\n"
                 "                               PageRank over a crawl's link graph\n"
                 "\n"
                 "retrieval:\n"
                 "  query [options] <source> <query>\n"
                 "                               ranked results for a boolean query\n"
                 "      --limit <n>              how many results (default 10)\n"
                 "      --scorer bm25|tfidf      which ranking function\n"
                 "      --snippet                show an excerpt under each result\n"
                 "      --corpus <path>          where to read snippet text from\n"
                 "      --max-chars <n>          how long an excerpt may be\n"
                 "      --tsv                    id and score, tab separated\n"
                 "  repl [options] <source>      read queries until end of input\n"
                 "\n"
                 "evaluation:\n"
                 "  evaluate [--cutoff <k>] [options] <source> <queries.jsonl> <qrels.tsv>\n"
                 "                               nDCG, precision, recall, F1, MRR, MAP at k\n"
                 "  bench build <source>         measure index construction\n"
                 "  bench query <source> <query_file>\n"
                 "                               measure query latency over a query log\n"
                 "      --stable                 print only run-to-run stable figures\n"
                 "      --threads <n>            build threads (bench build)\n"
                 "      --limit <n>              results per query (bench query)\n"
                 "      --repeats <n>            times to run each query\n"
                 "      --snippet [--corpus <p>] measure with excerpts too\n"
                 "\n"
                 "<source> is a corpus directory, a .corpus file or a saved index file.\n";
    return 2;
}

// Reports a corpus directory that does not exist. Returns the exit code.
int reject_missing_corpus(const std::filesystem::path& corpus_dir) {
    std::cerr << "search: not a directory: " << corpus_dir.string() << "\n";
    return 1;
}

bool parse_count(const std::string& text, std::size_t& out) {
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(text, &consumed);
        if (consumed != text.size() || parsed < 0) {
            return false;
        }
        out = static_cast<std::size_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool take_threads(std::vector<std::string>& args, std::size_t& threads) {
    if (args.size() < 2 || args.front() != "--threads") {
        return true;
    }
    if (!parse_count(args[1], threads) || threads == 0) {
        std::cerr << "search: threads must be a positive integer: " << args[1] << "\n";
        return false;
    }
    args.erase(args.begin(), args.begin() + 2);
    return true;
}

bool looks_like_corpus_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[4] = {};
    return static_cast<bool>(in.read(magic, 4)) && magic[0] == 'S' && magic[1] == 'C' &&
           magic[2] == 'P' && magic[3] == 'S';
}

std::optional<InvertedIndex> open_index(const std::filesystem::path& source,
                                        std::size_t threads = 1) {
    std::error_code ec;
    if (std::filesystem::is_directory(source, ec)) {
        return build_index_parallel(source, threads);
    }

    if (std::filesystem::is_regular_file(source, ec)) {
        if (looks_like_corpus_file(source)) {
            return build_index_parallel(source, threads);
        }

        std::string error;
        std::optional<InvertedIndex> index = read_index_file(source, error);
        if (!index.has_value()) {
            std::cerr << "search: " << error << "\n";
        }
        return index;
    }

    reject_missing_corpus(source);
    return std::nullopt;
}

void print_terms(const std::vector<Token>& tokens) {
    for (const Token& token : tokens) {
        std::cout << token.position << " " << token.offset << " " << token.text << "\n";
    }
}

bool read_text_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "search: cannot read " << path.string() << "\n";
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

bool take_query_options(std::vector<std::string>& args, QueryOptions& options) {
    while (!args.empty() && args.front().starts_with("--")) {
        const std::string flag = args.front();

        if (flag == "--snippet") {
            options.snippets = true;
            args.erase(args.begin());
            continue;
        }
        if (flag == "--tsv") {
            options.tsv = true;
            args.erase(args.begin());
            continue;
        }

        if (args.size() < 2) {
            return false;
        }
        const std::string value = args[1];

        if (flag == "--limit") {
            if (!parse_count(value, options.limit)) {
                std::cerr << "search: limit must be a non-negative integer: " << value << "\n";
                return false;
            }
        } else if (flag == "--max-chars") {
            if (!parse_count(value, options.snippet_chars) || options.snippet_chars == 0) {
                std::cerr << "search: max-chars must be a positive integer: " << value << "\n";
                return false;
            }
        } else if (flag == "--corpus") {
            options.corpus = value;
        } else if (flag == "--scorer") {
            if (value == "bm25") {
                options.scorer = Scorer::Bm25;
            } else if (value == "tfidf") {
                options.scorer = Scorer::TfIdf;
            } else {
                std::cerr << "search: unknown scorer: " << value << "\n";
                return false;
            }
        } else {
            std::cerr << "search: unknown option: " << flag << "\n";
            return false;
        }

        args.erase(args.begin(), args.begin() + 2);
    }

    return true;
}

// Where to read document text from when a query asks for snippets.
//
// An index file does not hold the text a snippet is cut from, so querying one
// used to accept --snippet and print nothing. A corpus directory or .corpus
// file is its own answer; for an index file, the corpus that built it is
// looked for beside it under the same stem, which is what index-write leaves
// there. `--corpus` overrides both.
std::optional<std::filesystem::path> corpus_for_snippets(const std::string& source,
                                                         const QueryOptions& options) {
    if (!options.corpus.empty()) {
        return std::filesystem::path(options.corpus);
    }

    std::error_code ec;
    const std::filesystem::path path(source);
    if (std::filesystem::is_directory(path, ec) || looks_like_corpus_file(path)) {
        return path;
    }

    std::filesystem::path beside = path;
    beside.replace_extension(".corpus");
    if (beside != path && std::filesystem::is_regular_file(beside, ec)) {
        return beside;
    }
    return std::nullopt;
}

// Opens the corpus for snippets, or returns nullptr when there is none. A
// corpus that cannot be opened is not an error: the query still answers, it
// just answers without excerpts.
std::unique_ptr<CorpusReader> open_snippet_corpus(const std::string& source,
                                                  const QueryOptions& options, bool wanted) {
    if (!wanted) {
        return nullptr;
    }

    const std::optional<std::filesystem::path> path = corpus_for_snippets(source, options);
    if (!path.has_value()) {
        return nullptr;
    }

    std::string error;
    return open_corpus(*path, error);
}

int cmd_wiki_import(const std::vector<std::string>& args) {
    std::vector<std::string> rest;
    std::size_t limit = 0;
    WikiField field = WikiField::Text;

    for (std::size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--abstracts") {
            field = WikiField::OpeningText;
            continue;
        }
        if (args[i] == "--limit") {
            if (i + 1 >= args.size() || !parse_count(args[i + 1], limit)) {
                std::cerr << "search: limit must be a non-negative integer\n";
                return 1;
            }
            i++;
            continue;
        }
        if (args[i].starts_with("--")) {
            std::cerr << "search: unknown option: " << args[i] << "\n";
            return 1;
        }
        rest.push_back(args[i]);
    }

    if (rest.size() != 2) {
        return usage();
    }

    WikiImportReport report;
    std::string error;
    bool ok = false;

    if (rest[0] == "-") {
        ok = import_cirrussearch(std::cin, rest[1], field, limit, report, error);
    } else {
        std::ifstream dump(rest[0], std::ios::binary);
        if (!dump) {
            std::cerr << "search: cannot read " << rest[0] << "\n";
            return 1;
        }
        ok = import_cirrussearch(dump, rest[1], field, limit, report, error);
    }

    if (!ok) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    std::cout << "lines " << report.lines_seen << "\n"
              << "documents " << report.documents_written << "\n"
              << "skipped_empty " << report.skipped_empty << "\n";
    return 0;
}

int cmd_corpus_write(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(args[0], ec)) {
        return reject_missing_corpus(args[0]);
    }

    std::string error;
    if (!write_corpus_file(args[0], args[1], error)) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }
    return 0;
}

int cmd_beir_import(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }

    BeirImportReport report;
    std::string error;
    if (!import_beir(args[0], args[1], report, error)) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    std::cout << "lines " << report.lines_seen << "\n"
              << "documents " << report.documents_written << "\n"
              << "skipped " << report.skipped_empty << "\n";
    return 0;
}

int cmd_evaluate(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;

    // --cutoff is this command's own, so it is taken before the query options
    // that query, repl and evaluate share.
    std::size_t cutoff = 10;
    for (std::size_t i = 0; i + 1 < rest.size(); i++) {
        if (rest[i] != "--cutoff") {
            continue;
        }
        if (!parse_count(rest[i + 1], cutoff) || cutoff == 0) {
            std::cerr << "search: cutoff must be a positive integer: " << rest[i + 1] << "\n";
            return 1;
        }
        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i),
                   rest.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        break;
    }

    QueryOptions options;
    if (!take_query_options(rest, options)) {
        return 1;
    }
    if (rest.size() != 3) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(rest[0]);
    if (!opened.has_value()) {
        return 1;
    }

    RelevanceReport report;
    std::string error;
    if (!evaluate_relevance(*opened, rest[1], rest[2], options, cutoff, report, error)) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    const std::string k = std::to_string(report.cutoff);
    std::cout << "queries " << report.queries << "\n"
              << "unjudged " << report.unjudged << "\n"
              << "scorer " << (options.scorer == Scorer::Bm25 ? "bm25" : "tfidf") << "\n"
              << "cutoff " << report.cutoff << "\n"
              << std::fixed << std::setprecision(4)
              << "ndcg_at_" << k << " " << report.ndcg_at_k << "\n"
              << "precision_at_" << k << " " << report.precision_at_k << "\n"
              << "max_precision_at_" << k << " " << report.max_precision_at_k << "\n"
              << "recall_at_" << k << " " << report.recall_at_k << "\n"
              << "f1_at_" << k << " " << report.f1_at_k << "\n"
              << "mrr_at_" << k << " " << report.mrr_at_k << "\n"
              << "map " << report.mean_average_precision << "\n"
              << "r_precision " << report.r_precision << "\n"
              << "recall_at_100 " << report.recall_at_100 << "\n";
    return 0;
}


int cmd_bench(const std::vector<std::string>& args) {
    std::vector<std::string> rest;
    bool stable_only = false;
    std::size_t threads = 1;
    std::size_t limit = 10;
    std::size_t repeats = 1;
    bool snippets = false;
    std::string snippet_corpus;

    for (std::size_t i = 0; i < args.size(); i++) {
        const std::string& argument = args[i];
        if (!argument.starts_with("--")) {
            rest.push_back(argument);
            continue;
        }

        if (argument == "--stable") {
            stable_only = true;
            continue;
        }
        if (argument == "--snippet") {
            snippets = true;
            continue;
        }

        if (i + 1 >= args.size()) {
            return usage();
        }

        // The only flag here whose value is not a number: where snippet text
        // comes from, when it is not beside the index.
        if (argument == "--corpus") {
            snippet_corpus = args[i + 1];
            i++;
            continue;
        }

        std::size_t value = 0;
        if (!parse_count(args[i + 1], value)) {
            std::cerr << "search: expected a number after " << argument << ": " << args[i + 1]
                      << "\n";
            return 1;
        }

        if (argument == "--threads") {
            if (value == 0) {
                std::cerr << "search: threads must be a positive integer\n";
                return 1;
            }
            threads = value;
        } else if (argument == "--limit") {
            limit = value;
        } else if (argument == "--repeats") {
            if (value == 0) {
                std::cerr << "search: repeats must be a positive integer\n";
                return 1;
            }
            repeats = value;
        } else {
            std::cerr << "search: unknown option: " << argument << "\n";
            return 1;
        }

        i++;
    }

    if (rest.size() < 2) {
        return usage();
    }

    const std::string& what = rest[0];
    const std::filesystem::path source = rest[1];

    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec) &&
        !std::filesystem::is_regular_file(source, ec)) {
        return reject_missing_corpus(source);
    }

    if (what == "build") {
        if (rest.size() != 2) {
            return usage();
        }
        if (!std::filesystem::is_directory(source, ec) && !looks_like_corpus_file(source)) {
            std::cerr << "search: bench build needs a corpus directory or a .corpus file: "
                      << source.string() << "\n";
            return 1;
        }
        print_measurements(std::cout, bench_environment(source), stable_only);
        print_measurements(std::cout, bench_build(source, threads), stable_only);
        return 0;
    }

    if (what == "query") {
        if (rest.size() != 3) {
            return usage();
        }

        std::string text;
        if (!read_text_file(rest[2], text)) {
            return 1;
        }

        std::vector<std::string> queries;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            const std::size_t begin = line.find_first_not_of(" \t\r");
            if (begin == std::string::npos) {
                continue;
            }
            const std::size_t end = line.find_last_not_of(" \t\r");
            queries.push_back(line.substr(begin, end - begin + 1));
        }

        const std::optional<InvertedIndex> opened = open_index(source, threads);
        if (!opened.has_value()) {
            return 1;
        }
        if (opened->document_count() == 0) {
            std::cerr << "search: bench query has nothing to search in " << source.string()
                      << "\n";
            return 1;
        }

        QueryOptions snippet_options;
        snippet_options.snippets = snippets;
        snippet_options.corpus = snippet_corpus;
        const std::unique_ptr<CorpusReader> corpus =
            open_snippet_corpus(source.string(), snippet_options, snippets);
        if (snippets && corpus == nullptr) {
            std::cerr << "search: bench query --snippet found no corpus beside " << source.string()
                      << "\n";
            return 1;
        }

        print_measurements(std::cout, bench_environment(source), stable_only);
        print_measurements(std::cout,
                           bench_query(*opened, corpus.get(), queries, limit, repeats, snippets),
                           stable_only);
        return 0;
    }

    std::cerr << "search: unknown benchmark: " << what << "\n";
    return 1;
}

int cmd_query(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    QueryOptions options;
    if (!take_query_options(rest, options)) {
        return 1;
    }
    if (rest.size() != 2) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(rest[0]);
    if (!opened.has_value()) {
        return 1;
    }

    std::string error;
    const std::optional<std::vector<QueryResult>> results =
        run_query(*opened, open_snippet_corpus(rest[0], options, options.snippets).get(), rest[1],
                  options, error);
    if (!results.has_value()) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    print_results(std::cout, *results, options);
    return 0;
}

int repl_meta_command(const std::string& line, QueryOptions& options, bool& quit) {
    std::istringstream words(line);
    std::string name;
    std::string value;
    words >> name >> value;

    if (name == ":quit" || name == ":q") {
        quit = true;
        return 0;
    }
    if (name == ":help") {
        std::cout << "queries use AND, OR, NOT, parentheses and \"phrases\"\n"
                     ":limit <n>      how many results to show\n"
                     ":scorer <name>  bm25 or tfidf\n"
                     ":snippet <on|off>\n"
                     ":settings       show the current settings\n"
                     ":quit\n";
        return 0;
    }
    if (name == ":settings") {
        std::cout << "limit " << options.limit << "\n"
                  << "scorer " << (options.scorer == Scorer::Bm25 ? "bm25" : "tfidf") << "\n"
                  << "snippet " << (options.snippets ? "on" : "off") << "\n";
        return 0;
    }
    if (name == ":limit") {
        if (!parse_count(value, options.limit)) {
            std::cout << "limit must be a non-negative integer\n";
        }
        return 0;
    }
    if (name == ":scorer") {
        if (value == "bm25") {
            options.scorer = Scorer::Bm25;
        } else if (value == "tfidf") {
            options.scorer = Scorer::TfIdf;
        } else {
            std::cout << "scorer must be bm25 or tfidf\n";
        }
        return 0;
    }
    if (name == ":snippet") {
        if (value == "on") {
            options.snippets = true;
        } else if (value == "off") {
            options.snippets = false;
        } else {
            std::cout << "snippet must be on or off\n";
        }
        return 0;
    }

    std::cout << "unknown command: " << name << "\n";
    return 0;
}

int cmd_repl(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    QueryOptions options;
    if (!take_query_options(rest, options)) {
        return 1;
    }
    if (rest.size() != 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(rest[0]);
    if (!opened.has_value()) {
        return 1;
    }

    // The REPL can turn snippets on later with :snippet, so the corpus is
    // opened up front rather than at the first query that needs it.
    const std::unique_ptr<CorpusReader> corpus = open_snippet_corpus(rest[0], options, true);
    const bool interactive = isatty(STDIN_FILENO) != 0;

    std::string line;
    while (true) {
        if (interactive) {
            std::cout << "> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            continue;
        }
        const std::size_t end = line.find_last_not_of(" \t");
        line = line.substr(begin, end - begin + 1);

        if (line.front() == ':') {
            bool quit = false;
            repl_meta_command(line, options, quit);
            if (quit) {
                break;
            }
            continue;
        }

        std::string error;
        const std::optional<std::vector<QueryResult>> results =
            run_query(*opened, corpus.get(), line, options, error);
        if (!results.has_value()) {
            std::cout << "error: " << error << "\n";
            continue;
        }

        print_results(std::cout, *results, options);
    }

    return 0;
}


int cmd_pagerank(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    std::size_t iterations = 30;
    double damping = 0.85;

    while (rest.size() >= 2 && rest.front().starts_with("--")) {
        const std::string flag = rest.front();
        if (flag == "--iterations") {
            if (!parse_count(rest[1], iterations)) {
                std::cerr << "search: iterations must be a non-negative integer: " << rest[1]
                          << "\n";
                return 1;
            }
        } else if (flag == "--damping") {
            try {
                std::size_t consumed = 0;
                damping = std::stod(rest[1], &consumed);
                if (consumed != rest[1].size() || damping < 0.0 || damping > 1.0) {
                    throw std::invalid_argument("damping");
                }
            } catch (const std::exception&) {
                std::cerr << "search: damping must be between 0 and 1: " << rest[1] << "\n";
                return 1;
            }
        } else {
            std::cerr << "search: unknown option: " << flag << "\n";
            return 1;
        }
        rest.erase(rest.begin(), rest.begin() + 2);
    }

    if (rest.size() != 1) {
        return usage();
    }

    std::string text;
    if (!read_text_file(rest[0], text)) {
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> links;
    std::size_t line_start = 0;
    while (line_start < text.size()) {
        const std::size_t line_end = text.find('\n', line_start);
        const std::string line =
            text.substr(line_start, line_end == std::string::npos ? std::string::npos
                                                                  : line_end - line_start);
        const std::size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            links.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        if (line_end == std::string::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    std::cout << std::fixed << std::setprecision(6);
    for (const RankedPage& page : pagerank(links, iterations, damping)) {
        std::cout << page.page << " " << page.score << "\n";
    }
    return 0;
}

int cmd_crawl(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    CrawlOptions options;
    std::optional<std::filesystem::path> mirror;
    bool delay_given = false;

    while (rest.size() >= 1 && rest.front().starts_with("--")) {
        const std::string flag = rest.front();

        if (flag == "--all-hosts") {
            options.same_host_only = false;
            rest.erase(rest.begin());
            continue;
        }

        if (rest.size() < 2) {
            return usage();
        }
        const std::string value = rest[1];

        if (flag == "--mirror") {
            mirror = value;
        } else if (flag == "--out") {
            options.output_dir = value;
        } else if (flag == "--user-agent") {
            options.user_agent = value;
        } else if (flag == "--max-pages") {
            if (!parse_count(value, options.max_pages)) {
                std::cerr << "search: max-pages must be a non-negative integer: " << value << "\n";
                return 1;
            }
        } else if (flag == "--delay") {
            std::size_t milliseconds = 0;
            if (!parse_count(value, milliseconds)) {
                std::cerr << "search: delay must be a non-negative integer: " << value << "\n";
                return 1;
            }
            options.min_delay = std::chrono::milliseconds(static_cast<long long>(milliseconds));
            delay_given = true;
        } else if (flag == "--max-depth") {
            if (!parse_count(value, options.max_depth)) {
                std::cerr << "search: max-depth must be a non-negative integer: " << value << "\n";
                return 1;
            }
        } else {
            std::cerr << "search: unknown option: " << flag << "\n";
            return 1;
        }

        rest.erase(rest.begin(), rest.begin() + 2);
    }

    if (rest.size() != 1) {
        return usage();
    }

    const std::optional<Url> seed = parse_url(rest[0]);
    if (!seed.has_value()) {
        std::cerr << "search: not an http url: " << rest[0] << "\n";
        return 1;
    }

    std::unique_ptr<Fetcher> fetcher;
    if (mirror.has_value() && !delay_given) {
        options.min_delay = std::chrono::milliseconds(0);
    }
    if (mirror.has_value()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(*mirror, ec)) {
            return reject_missing_corpus(*mirror);
        }
        fetcher = make_file_fetcher(*mirror);
    } else {
        fetcher = make_http_fetcher(options.user_agent, 10);
        if (fetcher == nullptr) {
            std::cerr << "search: this build cannot fetch over http; pass --mirror\n";
            return 1;
        }
    }

    for (const CrawledPage& page : crawl(*seed, *fetcher, options)) {
        std::cout << page.depth << " " << page.status << " " << page.url << "\n";
    }
    return 0;
}

int cmd_index_write(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    IndexEncoding encoding = IndexEncoding::VarByte;
    if (rest.size() >= 2 && rest.front() == "--encoding") {
        const std::optional<IndexEncoding> parsed = parse_encoding(rest[1]);
        if (!parsed.has_value()) {
            std::cerr << "search: unknown encoding: " << rest[1] << "\n";
            return 1;
        }
        encoding = *parsed;
        rest.erase(rest.begin(), rest.begin() + 2);
    }
    std::size_t threads = 1;
    if (!take_threads(rest, threads)) {
        return 1;
    }
    if (rest.size() != 2) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(rest[0], threads);
    if (!opened.has_value()) {
        return 1;
    }

    std::string error;
    if (!write_index_file(*opened, rest[1], encoding, error)) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }
    return 0;
}

int cmd_index_size(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }

    const IndexEncoding encodings[] = {IndexEncoding::Plain, IndexEncoding::Delta,
                                       IndexEncoding::VarByte};
    const char* names[] = {"plain", "delta", "varbyte"};

    std::size_t plain_total = 0;
    for (std::size_t i = 0; i < 3; i++) {
        const IndexSizeReport report = measure_index(*opened, encodings[i]);
        if (i == 0) {
            plain_total = report.total;
        }

        std::cout << names[i] << " " << report.total
                  << " header " << report.header
                  << " documents " << report.documents
                  << " dictionary " << report.dictionary
                  << " postings " << report.postings
                  << " positions " << report.positions << "\n";
    }

    const IndexSizeReport best = measure_index(*opened, IndexEncoding::VarByte);
    std::cout << "ratio " << std::fixed << std::setprecision(2)
              << (best.total > 0 ? static_cast<double>(plain_total) / static_cast<double>(best.total)
                                 : 0.0)
              << "\n";
    return 0;
}

int cmd_analyze(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    print_terms(analyze(args[0]));
    return 0;
}

} 

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }

    const std::string command = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (command == "wiki-import") {
        return cmd_wiki_import(rest);
    }
    if (command == "beir-import") {
        return cmd_beir_import(rest);
    }
    if (command == "corpus-write") {
        return cmd_corpus_write(rest);
    }
    if (command == "crawl") {
        return cmd_crawl(rest);
    }
    if (command == "analyze") {
        return cmd_analyze(rest);
    }
    if (command == "index-write") {
        return cmd_index_write(rest);
    }
    if (command == "index-size") {
        return cmd_index_size(rest);
    }
    if (command == "pagerank") {
        return cmd_pagerank(rest);
    }
    if (command == "query") {
        return cmd_query(rest);
    }
    if (command == "repl") {
        return cmd_repl(rest);
    }
    if (command == "evaluate") {
        return cmd_evaluate(rest);
    }
    if (command == "bench") {
        return cmd_bench(rest);
    }

    std::cerr << "search: unknown command: " << command << "\n";
    return usage();
}
