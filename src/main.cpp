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
#include "evaluator.hpp"
#include "index.hpp"
#include "index_io.hpp"
#include "normalize.hpp"
#include "query.hpp"
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
                 "  index-stats <corpus_dir>     document, term and posting counts\n"
                 "  index-terms <corpus_dir>     every indexed term with its document frequency\n"
                 "  postings <corpus_dir> <word>  documents containing a word\n"
                 "  tf <corpus_dir> <word>       per-document counts for a word\n"
                 "  positions <corpus_dir> <word> token positions of a word per document\n"
                 "  lex <query>                  split a query into syntactic tokens\n"
                 "  parse <query>                parse a query into a boolean tree\n"
                 "  and <corpus_dir> <word>...   documents containing every word\n"
                 "  or <corpus_dir> <word>...    documents containing any of the words\n"
                 "  andnot <corpus_dir> <word> <word>\n"
                 "                               documents containing the first but not the second\n"
                 "  phrase <corpus_dir> <text>   documents containing the words consecutively\n"
                 "  match <corpus_dir> <query>   documents matching a boolean query\n"
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
                 "  lengths <corpus_dir>         indexed length of each document\n"
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

void print_document_ids(const InvertedIndex& index, const std::vector<std::size_t>& matches) {
    const std::vector<std::string>& ids = index.document_ids();
    for (const std::size_t doc_id : matches) {
        std::cout << ids[doc_id] << "\n";
    }
}

bool literal_doc_ids(const InvertedIndex& index, const std::string& word,
                     std::vector<std::size_t>& out) {
    const std::vector<Token> terms = analyze(word);
    if (terms.size() > 1) {
        std::cerr << "search: not a single term: " << word << "\n";
        return false;
    }

    out.clear();
    if (!terms.empty()) {
        for (const Posting& posting : index.postings(terms.front().text)) {
            out.push_back(posting.doc_id);
        }
    }
    return true;
}

template <typename Combine>
int run_set_query(const std::vector<std::string>& args, std::size_t min_words, Combine combine) {
    if (args.size() < min_words + 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    std::vector<std::size_t> result;
    if (!literal_doc_ids(index, args[1], result)) {
        return 1;
    }

    for (std::size_t i = 2; i < args.size(); i++) {
        std::vector<std::size_t> next;
        if (!literal_doc_ids(index, args[i], next)) {
            return 1;
        }
        result = combine(result, next);
    }

    print_document_ids(index, result);
    return 0;
}

int cmd_and(const std::vector<std::string>& args) {
    return run_set_query(args, 2, intersect);
}

int cmd_or(const std::vector<std::string>& args) {
    return run_set_query(args, 2, unite);
}

int cmd_andnot(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        return usage();
    }
    return run_set_query(args, 2, subtract);
}

int run_parsed_query(const std::vector<std::string>& args, const std::string& query) {
    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    const ParseResult parsed = parse_query(query);
    if (!parsed.root) {
        std::cerr << "search: " << parsed.error << "\n";
        return 1;
    }

    print_document_ids(index, evaluate(*parsed.root, index));
    return 0;
}

int cmd_match(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }
    return run_parsed_query(args, args[1]);
}

int cmd_phrase(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return usage();
    }
    if (args[1].find('"') != std::string::npos) {
        std::cerr << "search: a phrase may not contain a quote\n";
        return 1;
    }
    return run_parsed_query(args, "\"" + args[1] + "\"");
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



int cmd_lengths(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    const std::vector<std::string>& ids = index.document_ids();
    for (std::size_t doc_id = 0; doc_id < ids.size(); doc_id++) {
        std::cout << ids[doc_id] << " " << index.document_length(doc_id) << "\n";
    }

    std::cout << "average " << std::fixed << std::setprecision(2)
              << index.average_document_length() << "\n";
    return 0;
}






int cmd_lex(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    std::vector<QueryToken> tokens;
    std::string error;
    if (!lex_query(args[0], tokens, error)) {
        std::cerr << "search: " << error << "\n";
        return 1;
    }

    for (const QueryToken& token : tokens) {
        switch (token.kind) {
        case QueryTokenKind::Term:   std::cout << "term " << token.text << "\n"; break;
        case QueryTokenKind::Phrase: std::cout << "phrase " << token.text << "\n"; break;
        case QueryTokenKind::And:    std::cout << "and\n"; break;
        case QueryTokenKind::Or:     std::cout << "or\n"; break;
        case QueryTokenKind::Not:    std::cout << "not\n"; break;
        case QueryTokenKind::LParen: std::cout << "lparen\n"; break;
        case QueryTokenKind::RParen: std::cout << "rparen\n"; break;
        }
    }
    return 0;
}

int cmd_parse(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const ParseResult result = parse_query(args[0]);
    if (!result.root) {
        std::cerr << "search: " << result.error << "\n";
        return 1;
    }

    std::cout << to_sexpr(*result.root) << "\n";
    return 0;
}

template <typename Emit>
int run_term_query(const std::vector<std::string>& args, Emit emit) {
    if (args.size() != 2) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    const std::vector<Token> query = analyze(args[1]);
    if (query.size() > 1) {
        std::cerr << "search: not a single term: " << args[1] << "\n";
        return 1;
    }
    if (query.empty()) {
        return 0;
    }

    const std::vector<std::string>& ids = index.document_ids();
    for (const Posting& posting : index.postings(query.front().text)) {
        emit(ids[posting.doc_id], posting);
    }
    return 0;
}

int cmd_postings(const std::vector<std::string>& args) {
    return run_term_query(args, [](const std::string& id, const Posting&) {
        std::cout << id << "\n";
    });
}

int cmd_tf(const std::vector<std::string>& args) {
    return run_term_query(args, [](const std::string& id, const Posting& posting) {
        std::cout << id << " " << posting.frequency << "\n";
    });
}

int cmd_positions(const std::vector<std::string>& args) {
    return run_term_query(args, [](const std::string& id, const Posting& posting) {
        std::cout << id;
        for (const std::size_t position : posting.positions) {
            std::cout << " " << position;
        }
        std::cout << "\n";
    });
}

int cmd_index_stats(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    std::cout << "documents: " << index.document_count() << "\n"
              << "terms: " << index.term_count() << "\n"
              << "postings: " << index.posting_count() << "\n";
    return 0;
}

int cmd_index_terms(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        return usage();
    }

    const std::optional<InvertedIndex> opened = open_index(args[0]);
    if (!opened.has_value()) {
        return 1;
    }
    const InvertedIndex& index = *opened;

    for (const std::string& term : index.terms()) {
        std::cout << term << " " << index.document_frequency(term) << "\n";
    }
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
    if (command == "index-stats") {
        return cmd_index_stats(rest);
    }
    if (command == "index-terms") {
        return cmd_index_terms(rest);
    }
    if (command == "postings") {
        return cmd_postings(rest);
    }
    if (command == "tf") {
        return cmd_tf(rest);
    }
    if (command == "positions") {
        return cmd_positions(rest);
    }
    if (command == "lex") {
        return cmd_lex(rest);
    }
    if (command == "parse") {
        return cmd_parse(rest);
    }
    if (command == "and") {
        return cmd_and(rest);
    }
    if (command == "or") {
        return cmd_or(rest);
    }
    if (command == "andnot") {
        return cmd_andnot(rest);
    }
    if (command == "phrase") {
        return cmd_phrase(rest);
    }
    if (command == "match") {
        return cmd_match(rest);
    }
    if (command == "lengths") {
        return cmd_lengths(rest);
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
