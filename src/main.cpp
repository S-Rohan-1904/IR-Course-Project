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
#include "corpus.hpp"
#include "corpus_file.hpp"
#include "document.hpp"
#include "normalize.hpp"
#include "thread_pool.hpp"
#include "thread_pool.hpp"
#include "stemmer.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

namespace {

int usage() {
    std::cerr << "usage: search <command> [args...]\n"
                 "\n"
                 "commands:\n"
                 "  docs <corpus_dir>            list the document ids in a corpus\n"
                 "  show <corpus_dir> <doc_id>   print one document's id, title and body\n"
                 "  tokenize <text>              split text into tokens\n"
                 "  normalize <token>            reduce one token to its indexed term\n"
                 "  terms [--drop-stopwords] <text>\n"
                 "                               tokenize, then normalize\n"
                 "  stopword <word>              is this word a stopword?\n"
                 "  stem-info <word>             Porter consonant pattern and measure\n"
                 "  stem-step <step> <word>      apply one Porter step\n"
                 "                               (1a, 1b, 1c, 2, 3, 4, 5a, 5b)\n"
                 "  stem <word>                  reduce a word to its Porter stem\n"
                 "  analyze <text>               run the full analysis chain\n"
                 "  analyze-doc <corpus_dir> <doc_id>\n"
                 "                               analyze one document from disk\n"
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
                 "  pool-sum <threads> <n>       sum of squares below n, on a thread pool\n"
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

int cmd_tokenize(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    for (const Token& token : tokenize(args[0])) {
        std::cout << token.position << " " << token.offset << " " << token.text << "\n";
    }
    return 0;
}

int cmd_normalize(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    std::cout << normalize(args[0]) << "\n";
    return 0;
}

int cmd_terms(const std::vector<std::string>& args) {
    std::vector<std::string> rest = args;
    bool drop_stopwords = false;
    if (!rest.empty() && rest.front() == "--drop-stopwords") {
        drop_stopwords = true;
        rest.erase(rest.begin());
    }
    if (rest.size() != 1) {
        return usage();
    }

    std::vector<Token> tokens = normalize_tokens(tokenize(rest[0]));
    if (drop_stopwords) {
        tokens = remove_stopwords(std::move(tokens));
    }

    for (const Token& token : tokens) {
        std::cout << token.position << " " << token.offset << " " << token.text << "\n";
    }
    return 0;
}

int cmd_stopword(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    std::cout << (is_stopword(normalize(args[0])) ? "yes" : "no") << "\n";
    return 0;
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





















int cmd_pool_sum(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }

    std::size_t threads = 0;
    std::size_t count = 0;
    if (!parse_count(args[0], threads) || threads == 0) {
        std::cerr << "search: threads must be a positive integer: " << args[0] << "\n";
        return 1;
    }
    if (!parse_count(args[1], count)) {
        std::cerr << "search: count must be a non-negative integer: " << args[1] << "\n";
        return 1;
    }

    ThreadPool pool(threads);
    std::vector<std::future<std::size_t>> pending;
    pending.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        pending.push_back(pool.submit([i] { return i * i; }));
    }

    std::size_t total = 0;
    for (std::future<std::size_t>& future : pending) {
        total += future.get();
    }

    std::cout << "threads " << pool.size() << "\n"
              << "tasks " << count << "\n"
              << "sum " << total << "\n";
    return 0;
}

















int cmd_analyze(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    print_terms(analyze(args[0]));
    return 0;
}

int cmd_analyze_doc(const std::vector<std::string>& args) {
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

    const std::optional<Document> doc = corpus->read(id);
    if (!doc.has_value()) {
        std::cerr << "search: no such document: " << id << "\n";
        return 1;
    }

    print_terms(analyze_document(*doc));
    return 0;
}

int cmd_stem(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    std::cout << porter::stem(args[0]) << "\n";
    return 0;
}

int cmd_stem_step(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }

    const std::string& step = args[0];
    const std::string& word = args[1];

    if (step == "1a") {
        std::cout << porter::step_1a_plurals(word) << "\n";
        return 0;
    }
    if (step == "1b") {
        std::cout << porter::step_1b_verb_endings(word) << "\n";
        return 0;
    }
    if (step == "1c") {
        std::cout << porter::step_1c_y_to_i(word) << "\n";
        return 0;
    }
    if (step == "2") {
        std::cout << porter::step_2_double_suffixes(word) << "\n";
        return 0;
    }
    if (step == "3") {
        std::cout << porter::step_3_derived_suffixes(word) << "\n";
        return 0;
    }
    if (step == "4") {
        std::cout << porter::step_4_residual_suffixes(word) << "\n";
        return 0;
    }
    if (step == "5a") {
        std::cout << porter::step_5a_final_e(word) << "\n";
        return 0;
    }
    if (step == "5b") {
        std::cout << porter::step_5b_double_l(word) << "\n";
        return 0;
    }

    std::cerr << "search: unknown stemmer step: " << step << "\n";
    return 1;
}

int cmd_stem_info(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const std::string& word = args[0];

    std::string pattern;
    pattern.reserve(word.size());
    for (std::size_t i = 0; i < word.size(); ++i) {
        pattern.push_back(porter::is_consonant(word, i) ? 'C' : 'V');
    }

    std::cout << "pattern: " << pattern << "\n"
              << "measure: " << porter::measure(word) << "\n"
              << "vowel: " << (porter::contains_vowel(word) ? "yes" : "no") << "\n"
              << "double: " << (porter::ends_with_double_consonant(word) ? "yes" : "no") << "\n"
              << "cvc: " << (porter::ends_cvc(word) ? "yes" : "no") << "\n";
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
    if (command == "tokenize") {
        return cmd_tokenize(rest);
    }
    if (command == "normalize") {
        return cmd_normalize(rest);
    }
    if (command == "terms") {
        return cmd_terms(rest);
    }
    if (command == "stopword") {
        return cmd_stopword(rest);
    }
    if (command == "stem-info") {
        return cmd_stem_info(rest);
    }
    if (command == "stem") {
        return cmd_stem(rest);
    }
    if (command == "analyze") {
        return cmd_analyze(rest);
    }
    if (command == "corpus-write") {
        return cmd_corpus_write(rest);
    }
    if (command == "pool-sum") {
        return cmd_pool_sum(rest);
    }
    if (command == "analyze-doc") {
        return cmd_analyze_doc(rest);
    }
    if (command == "stem-step") {
        return cmd_stem_step(rest);
    }

    std::cerr << "search: unknown command: " << command << "\n";
    return usage();
}
