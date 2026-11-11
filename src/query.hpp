#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class QueryTokenKind {
    Term,
    Phrase,
    And,
    Or,
    Not,
    LParen,
    RParen,
};

struct QueryToken {
    QueryTokenKind kind;
    std::string text;
};

bool lex_query(std::string_view query, std::vector<QueryToken>& out, std::string& error);

enum class QueryNodeKind {
    Term,
    Phrase,
    And,
    Or,
    Not,
};

struct QueryNode {
    QueryNodeKind kind;
    std::string text;
    std::vector<std::unique_ptr<QueryNode>> children;
};

struct ParseResult {
    std::unique_ptr<QueryNode> root;
    std::string error;
};

ParseResult parse_query(std::string_view query);

std::string to_sexpr(const QueryNode& node);
