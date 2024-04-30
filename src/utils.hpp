#ifndef UTILS_HPP
#define UTILS_HPP

#include <regex>
#include <string>

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

std::string
remove_comment_and_trailing_whitespace(std::string const& s)
{
  auto endpos = s.find('#');
  if (endpos == std::string::npos)
    endpos = s.size();
  while (endpos > 0 && s[endpos - 1] == ' ')
    endpos--;

  return s.substr(0, endpos);
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