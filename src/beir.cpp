#include "beir.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "corpus_file.hpp"
#include "jsonl.hpp"
#include "tokenizer.hpp"

namespace {

struct Record {
    std::string id;
    std::string text;
};

bool read_records(const std::filesystem::path& path, std::vector<Record>& out, std::string& error,
                  std::size_t& lines_seen, std::size_t& skipped_empty) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path.string();
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        lines_seen++;

        const std::optional<std::string> id = field_of(line, "_id");
        if (!id.has_value() || id->empty()) {
            skipped_empty++;
            continue;
        }

        const std::optional<std::string> title = field_of(line, "title");
        const std::optional<std::string> text = field_of(line, "text");

        std::string body = collapse(text.value_or(""));
        const std::string heading = collapse(title.value_or(""));
        if (heading.empty() && body.empty()) {
            skipped_empty++;
            continue;
        }

        out.push_back(Record{*id, heading.empty() ? body : heading + "\n\n" + body});
    }

    return true;
}

std::string as_document(const Record& record) {
    const std::size_t split = record.text.find("\n\n");
    if (split == std::string::npos) {
        return "title: \n\n" + record.text;
    }
    return "title: " + record.text.substr(0, split) + "\n\n" + record.text.substr(split + 2);
}

using Qrels = std::unordered_map<std::string, std::unordered_map<std::string, int>>;

bool read_qrels(const std::filesystem::path& path, Qrels& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path.string();
        return false;
    }

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (first) {
            first = false;
            if (line.starts_with("query-id")) {
                continue;
            }
        }

        std::istringstream fields(line);
        std::string query_id;
        std::string document_id;
        std::string score;
        if (!std::getline(fields, query_id, '\t') ||
            !std::getline(fields, document_id, '\t') || !std::getline(fields, score, '\t')) {
            continue;
        }

        int graded = 0;
        try {
            graded = std::stoi(score);
        } catch (const std::exception&) {
            continue;
        }
        if (graded > 0) {
            out[query_id][document_id] = graded;
        }
    }

    return true;
}

double gain(int graded) {
    return static_cast<double>(graded);
}

double discount(std::size_t rank) {
    return 1.0 / std::log2(static_cast<double>(rank) + 2.0);
}

double ndcg(const std::vector<QueryResult>& results,
            const std::unordered_map<std::string, int>& judged, std::size_t cutoff) {
    double actual = 0.0;
    for (std::size_t i = 0; i < results.size() && i < cutoff; i++) {
        const auto found = judged.find(results[i].document_id);
        if (found != judged.end()) {
            actual += gain(found->second) * discount(i);
        }
    }

    std::vector<int> ideal;
    ideal.reserve(judged.size());
    for (const auto& [document_id, graded] : judged) {
        ideal.push_back(graded);
    }
    std::sort(ideal.begin(), ideal.end(), std::greater<int>());

    double best = 0.0;
    for (std::size_t i = 0; i < ideal.size() && i < cutoff; i++) {
        best += gain(ideal[i]) * discount(i);
    }

    return best > 0.0 ? actual / best : 0.0;
}

std::string bag_of_words(std::string_view sentence) {
    std::string expression;
    for (const Token& token : tokenize(sentence)) {
        std::string word;
        for (const char c : token.text) {
            if (c == '"' || c == '(' || c == ')') {
                continue;
            }
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (word.empty()) {
            continue;
        }
        if (!expression.empty()) {
            expression += " OR ";
        }
        expression += word;
    }
    return expression;
}

}

bool import_beir(const std::filesystem::path& corpus_jsonl, const std::filesystem::path& output,
                 BeirImportReport& report, std::string& error) {
    report = BeirImportReport{};
    error.clear();

    std::vector<Record> records;
    if (!read_records(corpus_jsonl, records, error, report.lines_seen, report.skipped_empty)) {
        return false;
    }

    std::sort(records.begin(), records.end(),
              [](const Record& a, const Record& b) { return a.id < b.id; });

    std::optional<CorpusWriter> writer = CorpusWriter::create(output, error);
    if (!writer.has_value()) {
        return false;
    }

    for (const Record& record : records) {
        if (!writer->add(record.id, as_document(record))) {
            error = "cannot write " + output.string();
            return false;
        }
        report.documents_written++;
    }

    return writer->finish(error);
}

bool evaluate_relevance(const InvertedIndex& index, const std::filesystem::path& queries_jsonl,
                        const std::filesystem::path& qrels_tsv, const QueryOptions& options,
                        std::size_t cutoff, RelevanceReport& report, std::string& error) {
    report = RelevanceReport{};
    report.cutoff = cutoff;
    error.clear();

    if (cutoff == 0) {
        error = "cutoff must be at least 1";
        return false;
    }

    Qrels qrels;
    if (!read_qrels(qrels_tsv, qrels, error)) {
        return false;
    }

    std::ifstream in(queries_jsonl);
    if (!in) {
        error = "cannot read " + queries_jsonl.string();
        return false;
    }

    double precision_total = 0.0;
    double recall_at_k_total = 0.0;
    double f1_total = 0.0;
    double ndcg_total = 0.0;
    double reciprocal_rank_total = 0.0;
    double average_precision_total = 0.0;
    double r_precision_total = 0.0;
    double recall_at_100_total = 0.0;
    double max_precision_total = 0.0;

    std::string line;
    while (std::getline(in, line)) {
        const std::optional<std::string> id = field_of(line, "_id");
        const std::optional<std::string> text = field_of(line, "text");
        if (!id.has_value() || !text.has_value()) {
            continue;
        }

        const auto judged = qrels.find(*id);
        if (judged == qrels.end()) {
            report.unjudged++;
            continue;
        }

        const std::string expression = bag_of_words(*text);
        if (expression.empty()) {
            report.unjudged++;
            continue;
        }

        const std::size_t relevant = judged->second.size();
        QueryOptions run = options;
        run.limit = std::max(std::max<std::size_t>(100, cutoff), relevant);
        run.snippets = false;

        std::string query_error;
        const std::optional<std::vector<QueryResult>> results =
            run_query(index, nullptr, expression, run, query_error);
        if (!results.has_value()) {
            continue;
        }

        std::size_t hits_at_k = 0;
        std::size_t hits_at_100 = 0;
        std::size_t hits_at_r = 0;
        std::size_t hits_so_far = 0;
        std::size_t first_hit_rank = 0;
        double precision_sum_at_hits = 0.0;

        for (std::size_t i = 0; i < results->size(); i++) {
            if (judged->second.count((*results)[i].document_id) == 0) {
                continue;
            }
            hits_so_far++;
            const std::size_t rank = i + 1;
            if (first_hit_rank == 0) {
                first_hit_rank = rank;
            }
            if (rank <= cutoff) {
                hits_at_k++;
            }
            if (rank <= 100) {
                hits_at_100++;
            }
            if (rank <= relevant) {
                hits_at_r++;
            }
            precision_sum_at_hits += static_cast<double>(hits_so_far) /
                                     static_cast<double>(rank);
        }

        const double precision_at_k = static_cast<double>(hits_at_k) /
                                      static_cast<double>(cutoff);
        const double recall_at_k = static_cast<double>(hits_at_k) /
                                   static_cast<double>(relevant);

        precision_total += precision_at_k;
        recall_at_k_total += recall_at_k;
        if (precision_at_k + recall_at_k > 0.0) {
            f1_total += 2.0 * precision_at_k * recall_at_k / (precision_at_k + recall_at_k);
        }
        recall_at_100_total += static_cast<double>(hits_at_100) /
                               static_cast<double>(relevant);
        r_precision_total += static_cast<double>(hits_at_r) /
                             static_cast<double>(relevant);
        average_precision_total += precision_sum_at_hits / static_cast<double>(relevant);
        if (first_hit_rank != 0 && first_hit_rank <= cutoff) {
            reciprocal_rank_total += 1.0 / static_cast<double>(first_hit_rank);
        }
        max_precision_total += static_cast<double>(std::min(relevant, cutoff)) /
                               static_cast<double>(cutoff);
        ndcg_total += ndcg(*results, judged->second, cutoff);
        report.queries++;
    }

    if (report.queries > 0) {
        const double count = static_cast<double>(report.queries);
        report.precision_at_k = precision_total / count;
        report.recall_at_k = recall_at_k_total / count;
        report.f1_at_k = f1_total / count;
        report.ndcg_at_k = ndcg_total / count;
        report.mrr_at_k = reciprocal_rank_total / count;
        report.mean_average_precision = average_precision_total / count;
        report.r_precision = r_precision_total / count;
        report.recall_at_100 = recall_at_100_total / count;
        report.max_precision_at_k = max_precision_total / count;
    }

    return true;
}
