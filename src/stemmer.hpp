#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace porter {

bool is_consonant(std::string_view word, std::size_t index);

std::size_t measure(std::string_view word);

bool contains_vowel(std::string_view word);

bool ends_with_double_consonant(std::string_view word);

bool ends_cvc(std::string_view word);

std::string step_1a_plurals(std::string_view word);

std::string step_1b_verb_endings(std::string_view word);

std::string step_1c_y_to_i(std::string_view word);

std::string step_2_double_suffixes(std::string_view word);

std::string step_3_derived_suffixes(std::string_view word);

std::string step_4_residual_suffixes(std::string_view word);

std::string step_5a_final_e(std::string_view word);

std::string step_5b_double_l(std::string_view word);

std::string stem(std::string_view word);

}
