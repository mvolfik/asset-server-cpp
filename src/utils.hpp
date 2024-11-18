#ifndef UTILS_HPP
#define UTILS_HPP

#include <charconv>
#include <regex>
#include <string>
#include <string_view>

/**
 * Sanitize a string to be used as a filename: replace all non-alphanumeric
 * characters with '_'
 */
std::string
sanitize_filename(std::string_view const& s)
{
  std::string result;
  for (auto c : s) {
    if (std::isalnum(c) || c == '-' || c == '_')
      result += c;
    else
      result += '_';
  }
  return result;
}

/**
 * Create a string view that references the portion of the input string before
 * the first
 * '#' character, with any trailing whitespace removed.
 *
 * Example:
 *  remove_comment_and_trailing_whitespace("abc # def # ghi jkl") -> "abc"
 *  remove_comment_and_trailing_whitespace("abc    ") -> "abc"
 *  remove_comment_and_trailing_whitespace("abc   .#de") -> "abc   ."
 */
std::string_view
remove_comment_and_trailing_whitespace(std::string const& s)
{
  auto endpos = s.find('#');
  if (endpos == std::string::npos)
    endpos = s.size();
  while (endpos > 0 && s[endpos - 1] == ' ')
    endpos--;

  return std::string_view(s).substr(0, endpos);
}

int
string_view_to_int(std::string_view const& s)
{
  int result;
  auto err = std::from_chars(s.data(), s.data() + s.size(), result);
  if (err.ec == std::errc::invalid_argument)
    throw std::invalid_argument{ "invalid_argument" };
  if (err.ec == std::errc::result_out_of_range)
    throw std::out_of_range{ "out_of_range" };
  return result;
}

using dimension_t = unsigned long;

dimension_t
div_round_up(dimension_t a, dimension_t b)
{
  return (a + b - 1) / b;
}

dimension_t
div_round_close(dimension_t a, dimension_t b)
{
  return (a + b / 2) / b;
}

#endif // UTILS_HPP