#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <string>
#include <regex>

std::string
remove_suffix(std::string const& s)
{
  auto pos = s.find_last_of('.');
  if (pos == std::string_view::npos || pos < s.length() - 5)
    return s;
  return s.substr(0, pos);
}

std::string
json_sanitize_string(std::string const& s)
{
  auto without_backslash = std::regex_replace(s, std::regex("\\\\"), "\\\\");
  return std::regex_replace(
    std::move(without_backslash), std::regex("\""), "\\\"");
}

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

#endif // STRING_UTILS_HPP