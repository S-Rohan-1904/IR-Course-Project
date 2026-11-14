#include "snippet.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "analyzer.hpp"
#include "tokenizer.hpp"

namespace {

constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);

struct Word {
    std::size_t begin;
    std::size_t end;
    std::size_t term_index = kNoMatch;
    std::size_t highlight_begin = 0;
    std::size_t highlight_end = 0;
    std::size_t span = kNoMatch;
};

struct Occurrence {
    std::size_t phrase;
    std::size_t first;
    std::size_t last;
};

struct Span {
    std::size_t first;
    std::size_t last;
};

struct WindowScore {
    std::size_t phrases = 0;
    std::size_t distinct = 0;
    std::size_t total = 0;

    bool beats(const WindowScore& other) const {
        return std::tie(phrases, distinct, total) >
               std::tie(other.phrases, other.distinct, other.total);
    }

    bool empty() const { return phrases == 0 && distinct == 0 && total == 0; }
};

bool is_word_byte(char c) {
    const unsigned char byte = static_cast<unsigned char>(c);
    return byte >= 0x80 || std::isalnum(byte) != 0;
}

std::vector<Word> split_words(std::string_view text) {
    std::vector<Word> words;

    for (const Token& token : tokenize(text)) {
        std::size_t begin = token.offset;
        std::size_t end = token.offset + token.text.size();
        while (begin < end && !is_word_byte(text[begin])) {
            begin++;
        }
        while (end > begin && !is_word_byte(text[end - 1])) {
            end--;
        }

        Word word{token.offset, token.offset + token.text.size()};
        word.highlight_begin = begin;
        word.highlight_end = end;
        words.push_back(word);
    }

    return words;
}

std::vector<std::size_t> owning_words(const std::vector<Word>& words,
                                      const std::vector<Token>& tokens) {
    std::vector<std::size_t> owner;
    owner.reserve(tokens.size());

    for (const Token& token : tokens) {
        const auto after = std::upper_bound(
            words.begin(), words.end(), token.offset,
            [](std::size_t offset, const Word& word) { return offset < word.begin; });
        owner.push_back(after == words.begin()
                            ? 0
                            : static_cast<std::size_t>(after - words.begin()) - 1);
    }

    return owner;
}

void mark_terms(std::vector<Word>& words, const std::vector<Token>& tokens,
                const std::vector<std::size_t>& owner, const std::vector<std::string>& terms) {
    std::unordered_map<std::string, std::size_t> wanted;
    for (std::size_t i = 0; i < terms.size(); i++) {
        wanted.emplace(terms[i], i);
    }

    for (std::size_t i = 0; i < tokens.size(); i++) {
        const auto found = wanted.find(tokens[i].text);
        if (found != wanted.end()) {
            Word& word = words[owner[i]];
            word.term_index = std::min(word.term_index, found->second);
        }
    }
}

// A phrase occurs where the document's analysed tokens sit at the same relative positions
// as the phrase's own tokens. Positions keep the gaps left by stopwords, so this agrees
// with how the index matches phrases.
std::vector<Occurrence> find_occurrences(const std::vector<Token>& tokens,
                                         const std::vector<std::size_t>& owner,
                                         const std::vector<std::vector<Token>>& phrases) {
    std::unordered_map<std::size_t, std::size_t> at_position;
    at_position.reserve(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); i++) {
        at_position.emplace(tokens[i].position, i);
    }

    std::vector<Occurrence> found;
    for (std::size_t p = 0; p < phrases.size(); p++) {
        const std::vector<Token>& phrase = phrases[p];

        for (std::size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i].text != phrase.front().text) {
                continue;
            }

            Occurrence occurrence{p, owner[i], owner[i]};
            bool whole = true;
            for (std::size_t part = 1; part < phrase.size() && whole; part++) {
                const std::size_t wanted =
                    tokens[i].position + (phrase[part].position - phrase.front().position);
                const auto next = at_position.find(wanted);
                if (next == at_position.end() || tokens[next->second].text != phrase[part].text) {
                    whole = false;
                    continue;
                }
                occurrence.first = std::min(occurrence.first, owner[next->second]);
                occurrence.last = std::max(occurrence.last, owner[next->second]);
            }

            if (whole) {
                found.push_back(occurrence);
            }
        }
    }

    return found;
}

std::vector<Span> merge_spans(const std::vector<Occurrence>& occurrences) {
    std::vector<Span> sorted;
    sorted.reserve(occurrences.size());
    for (const Occurrence& occurrence : occurrences) {
        sorted.push_back(Span{occurrence.first, occurrence.last});
    }
    std::sort(sorted.begin(), sorted.end(), [](const Span& a, const Span& b) {
        return std::tie(a.first, a.last) < std::tie(b.first, b.last);
    });

    std::vector<Span> merged;
    for (const Span& span : sorted) {
        if (!merged.empty() && span.first <= merged.back().last) {
            merged.back().last = std::max(merged.back().last, span.last);
        } else {
            merged.push_back(span);
        }
    }
    return merged;
}

void append_word(std::string& out, std::string_view text, const Word& word, bool opens,
                 bool closes, const SnippetOptions& options) {
    if (!opens && !closes) {
        out.append(text.substr(word.begin, word.end - word.begin));
        return;
    }

    out.append(text.substr(word.begin, word.highlight_begin - word.begin));
    if (opens) {
        out += options.open;
    }
    out.append(text.substr(word.highlight_begin, word.highlight_end - word.highlight_begin));
    if (closes) {
        out += options.close;
    }
    out.append(text.substr(word.highlight_end, word.end - word.highlight_end));
}

std::string render(std::string_view text, const std::vector<Word>& words,
                   const std::vector<Span>& spans, std::size_t first, std::size_t last,
                   const SnippetOptions& options) {
    std::string out;

    if (words[first].begin > 0) {
        out += "...";
    }

    std::size_t cursor = words[first].begin;
    for (std::size_t i = first; i <= last; i++) {
        const Word& word = words[i];
        out.append(text.substr(cursor, word.begin - cursor));

        const bool highlightable = word.highlight_end > word.highlight_begin;
        if (word.span != kNoMatch) {
            const Span& span = spans[word.span];
            append_word(out, text, word, highlightable && i == std::max(span.first, first),
                        highlightable && i == std::min(span.last, last), options);
        } else {
            const bool matches = highlightable && word.term_index != kNoMatch;
            append_word(out, text, word, matches, matches, options);
        }
        cursor = word.end;
    }

    if (words[last].end < text.size()) {
        out += "...";
    }

    return out;
}

}

std::string make_snippet(std::string_view text, const std::vector<std::string>& terms,
                         const SnippetOptions& options) {
    return make_snippet(text, terms, {}, options);
}

std::string make_snippet(std::string_view text, const std::vector<std::string>& terms,
                         const std::vector<std::string>& phrases,
                         const SnippetOptions& options) {
    std::vector<Word> words = split_words(text);
    if (words.empty()) {
        return {};
    }

    std::vector<std::string> bare = terms;
    std::vector<std::vector<Token>> multi_word;
    for (const std::string& phrase : phrases) {
        std::vector<Token> parts = analyze(phrase);
        if (parts.size() == 1) {
            bare.push_back(parts.front().text);
        } else if (parts.size() > 1) {
            multi_word.push_back(std::move(parts));
        }
    }

    const std::vector<Token> tokens = analyze(text);
    const std::vector<std::size_t> owner = owning_words(words, tokens);
    mark_terms(words, tokens, owner, bare);

    const std::vector<Occurrence> occurrences = find_occurrences(tokens, owner, multi_word);
    const std::vector<Span> spans = merge_spans(occurrences);
    for (std::size_t s = 0; s < spans.size(); s++) {
        for (std::size_t i = spans[s].first; i <= spans[s].last; i++) {
            words[i].span = s;
        }
    }

    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> ending_at;
    if (!occurrences.empty()) {
        ending_at.resize(words.size());
        for (const Occurrence& occurrence : occurrences) {
            ending_at[occurrence.last].emplace_back(occurrence.first, occurrence.phrase);
        }
    }

    std::size_t best_first = 0;
    std::size_t best_last = 0;
    WindowScore best;

    for (std::size_t first = 0; first < words.size(); first++) {
        std::vector<bool> seen_term(bare.size(), false);
        std::vector<bool> seen_phrase(multi_word.size(), false);
        WindowScore score;
        std::size_t last = first;

        for (std::size_t i = first; i < words.size(); i++) {
            if (words[i].end - words[first].begin > options.max_chars) {
                break;
            }
            if (words[i].term_index != kNoMatch) {
                score.total++;
                if (!seen_term[words[i].term_index]) {
                    seen_term[words[i].term_index] = true;
                    score.distinct++;
                }
            }
            if (!ending_at.empty()) {
                for (const auto& [start, phrase] : ending_at[i]) {
                    if (start < first) {
                        continue;
                    }
                    score.total++;
                    if (!seen_phrase[phrase]) {
                        seen_phrase[phrase] = true;
                        score.phrases++;
                        score.distinct++;
                    }
                }
            }
            last = i;
        }

        if (score.beats(best)) {
            best = score;
            best_first = first;
            best_last = last;
        }
    }

    if (best.empty()) {
        best_first = 0;
        best_last = 0;
        for (std::size_t i = 0; i < words.size(); i++) {
            if (words[i].end - words[0].begin > options.max_chars) {
                break;
            }
            best_last = i;
        }
    }

    return render(text, words, spans, best_first, best_last, options);
}
