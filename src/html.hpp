#pragma once

#include <string>
#include <string_view>
#include <vector>

struct HtmlPage {
    std::string title;
    std::string text;
    std::vector<std::string> links;
};

HtmlPage parse_html(std::string_view html);

std::string decode_html_entities(std::string_view text);
