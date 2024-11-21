#ifndef UTILS_HPP
#define UTILS_HPP

#include <charconv>
#include <regex>
#include <string>
#include <string_view>

#include <openssl/sha.h>

#include <unidecode/unidecode.hpp>
#include <unidecode/utf8_string_iterator.hpp>

class Sanitizer
{
private:
  std::string result;

public:
  Sanitizer() {}

  using value_type = char;

  // implementing push_back and then using this with std::back_inserter is the
  // easiest way to hook this as an output iterator to unidecode::Unidecode
  void push_back(char c)
  {
    if (result.size() >= 64)
      return;

    if (std::isalnum(c) || c == '-' || c == '_')
      result += c;
    else
      result += '_';
  }

  std::string take_result() { return std::move(result); }
};

/**
 * Sanitize a string to be used as a filename: replace all non-alphanumeric
 * characters with '_'
 */
std::string
sanitize_filename(std::string_view s)
{
  Sanitizer sanitizer;
  unidecode::Unidecode(unidecode::Utf8StringIterator(s.begin()),
                       unidecode::Utf8StringIterator(s.end()),
                       std::back_inserter(sanitizer));
  return sanitizer.take_result();
}

/**
 * Create a string view that references the portion of the input string before
 * the last dot, eventually the whole string if no dot is found.
 */
std::string_view
get_filename_without_extension(std::string_view s)
{
  auto last_dot = s.find_last_of('.');
  if (last_dot == std::string::npos)
    return s;
  return s.substr(0, last_dot);
}

std::string_view
get_extension(std::string_view s)
{
  auto last_dot = s.find_last_of('.');
  if (last_dot == std::string::npos)
    return {};
  return s.substr(last_dot + 1);
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
string_view_to_int(std::string_view s)
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

static const char* hex_chars = "0123456789abcdef";

std::string
sha256(std::vector<std::uint8_t> const& data)
{
  std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
  SHA256(data.data(), data.size(), hash.data());
  // bytes to hex
  std::string result;
  for (auto c : hash) {
    result += hex_chars[c >> 4];
    result += hex_chars[c & 0xf];
  }
  return result;
}

#endif // UTILS_HPP