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

#include "corpus.hpp"
#include "corpus_file.hpp"
#include "document.hpp"

namespace {

int usage() {
    std::cerr << "usage: search <command> [args...]\n"
                 "\n"
                 "commands:\n"
                 "  docs <corpus_dir>            list the document ids in a corpus\n"
                 "  show <corpus_dir> <doc_id>   print one document's id, title and body\n"
                 "      --limit <n>              how many results (default 10)\n"
                 "      --scorer bm25|tfidf      which ranking function\n"
                 "      --snippet                show an excerpt under each result\n"
                 "      --corpus <path>          where to read snippet text from\n"
                 "      --max-chars <n>          how long an excerpt may be\n"
                 "      --tsv                    id and score, tab separated\n"
                 "  bench query <source> <query_file>\n"
                 "                               measure query latency over a query log\n"
                 "      --stable                 print only run-to-run stable figures\n"
                 "      --threads <n>            build threads (bench build)\n"
                 "      --limit <n>              results per query (bench query)\n"
                 "      --repeats <n>            times to run each query\n"
                 "      --snippet [--corpus <p>] measure with excerpts too\n"
                 "  corpus-write <corpus_dir> <file>\n"
                 "                               pack a corpus directory into one file\n"
                 "      --delay <ms>             floor between requests to one host\n"
                 "      --mirror <dir>           fetch from a mirrored site on disk\n"
                 "      --out <dir>              write the pages as a corpus\n"
                 "      --max-pages <n>          stop after n pages\n"
                 "      --max-depth <n>          follow links at most n deep\n"
                 "      --user-agent <name>      the name robots.txt is matched against\n"
                 "      --all-hosts              follow links off the seed host\n"
                 "\n"
                 "<source> is a corpus directory or a saved index file.\n";
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


int cmd_docs(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    std::string error;
    const std::unique_ptr<CorpusReader> corpus = open_corpus(args[0], error);
    if (!corpus) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    for (const std::string& id : corpus->document_ids()) {
        std::cout << id << "\n";
    }
    return 0;
}

int cmd_show(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }

    const std::string& id = args[1];

    std::string error;
    const std::unique_ptr<CorpusReader> corpus = open_corpus(args[0], error);
    if (!corpus) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    const std::vector<std::string>& ids = corpus->document_ids();
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        std::cerr << "search: no such document: " << id << "\n";
        return 1;
    }

    const std::optional<Document> doc = corpus->read(id);
    if (!doc.has_value()) {
        std::cerr << "search: malformed document: " << id << "\n";
        return 1;
    }

    std::cout << "id: " << doc->id << "\n"
              << "title: " << doc->title << "\n"
              << "\n"
              << doc->body << "\n";
    return 0;
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


// Where to read document text from when a query asks for snippets.
//
// An index file does not hold the text a snippet is cut from, so querying one
// used to accept --snippet and print nothing. A corpus directory or .corpus
// file is its own answer; for an index file, the corpus that built it is
// looked for beside it under the same stem, which is what index-write leaves
// there. `--corpus` overrides both.

// Opens the corpus for snippets, or returns nullptr when there is none. A
// corpus that cannot be opened is not an error: the query still answers, it
// just answers without excerpts.


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











































} 

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }

    const std::string command = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (command == "docs") {
        return cmd_docs(rest);
    }
    if (command == "show") {
        return cmd_show(rest);
    }
    if (command == "corpus-write") {
        return cmd_corpus_write(rest);
    }

    std::cerr << "search: unknown command: " << command << "\n";
    return usage();
}
