#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "utils.hpp"

/**
 * Size specification can be:
 * - single fixed value: then decrement is 0, decrement_is_pct is ignored,
 *   fixed_value specifies the width
 * - dynamic, decreasing sequence: fixed_value specifies the minimal width,
 *   decrement is non-zero
 *   - if decrement_is_pct, then the sequence is the original image size, and
 *     then each next size decremented by ceil(prev_size * decrement / 100)
 *     until (not including) the value is < fixed_value
 *   - else the sequence is original image size, then repeatedly subtracting
 *     decrement until (not including) the value is < fixed_value
 *
 * Size specification in the config file is a comma separated list of
 * size_specs, where each size_spec is:
 * - `123` -> single fixed size
 * - `123:10%` -> dynamic sequence, decrement is 10% of the previous size
 * - `123:10px` -> dynamic sequence, decrement is 10 pixels
 */
struct size_spec
{
  dimension_t fixed_value;
  dimension_t decrement;
  bool decrement_is_pct;

  static size_spec parse(std::string s)
  {
    size_spec spec;
    auto colon_pos = s.find(':');
    if (colon_pos == std::string::npos) {
      spec.fixed_value = std::stoi(s);
      spec.decrement = 0;
      spec.decrement_is_pct = false;
    } else {
      spec.fixed_value = std::stoi(s.substr(0, colon_pos));
      auto decrement_str = s.substr(colon_pos + 1);
      if (decrement_str.back() == '%') {
        spec.decrement_is_pct = true;
        decrement_str.pop_back();
      } else {
        auto back = decrement_str.back();
        if (back != 'x' && --back != 'p')
          throw std::runtime_error(
            "Invalid size spec, expected 'px' or '%' after number: " + s);

        spec.decrement_is_pct = false;
        decrement_str.pop_back();
        decrement_str.pop_back();
      }
      spec.decrement = std::stoi(decrement_str);
      if (spec.decrement_is_pct && spec.decrement >= 100)
        throw std::runtime_error(
          "Percentual decrement must be smaller than 100: " + s);
      if (spec.decrement == 0)
        throw std::runtime_error("Decrement must be greater than 0: " + s);
    }
    return spec;
  }

  void get_sizes(dimension_t original_width,
                 std::set<dimension_t>& result) const
  {
    if (decrement == 0) {
      result.insert(fixed_value);
      return;
    }

    dimension_t width = original_width;
    while (width >= fixed_value) {
      result.insert(width);
      dimension_t decrement = this->decrement;

      if (decrement_is_pct)
        decrement = div_round_up(width * decrement, 100);

      if (decrement > width) // avoid underflow
        break;
      width -= decrement;
    }
  }
};

unsigned
parse_bytes(std::string const& s)
{
  unsigned val = 0;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] < '0' || s[i] > '9') {
      if (i != s.size() - 1)
        throw std::runtime_error("Failed to parse value: " + s);

      switch (s[i]) {
        case 'k':
        case 'K':
          val *= 1024;
          break;
        case 'M':
          val *= 1024 * 1024;
          break;
        case 'G':
          val *= 1024 * 1024 * 1024;
          break;
        default:
          throw std::runtime_error("Invalid byte value suffix: " + s);
      }
      return val;
    }
    val = val * 10 + (s[i] - '0');
  }
  throw std::runtime_error("Missing byte value suffix: " + s);
  return val;
}

struct config
{
  unsigned processing_timeout_secs = 8;
  unsigned socket_kill_timeout_secs = 10;

  std::optional<unsigned> thread_pool_size;

  unsigned upload_limit_bytes = 20 * 1024 * 1024;

  std::vector<size_spec> sizes;

  std::unordered_map<std::string, std::vector<std::string>> formats;
  static constexpr const char* ALL_FORMATS_KEY = "*";

  unsigned get_thread_pool_size() const
  {
    return thread_pool_size.value_or(std::thread::hardware_concurrency() + 1);
  }

  std::set<dimension_t> get_sizes(dimension_t original_width) const
  {
    std::set<dimension_t> result;
    result.insert(original_width);

    for (auto const& spec : sizes) {
      spec.get_sizes(original_width, result);
    }
    return result;
  }

  std::vector<std::string> get_formats(std::string const& format) const
  {
    std::vector<std::string> result;
    auto it = formats.find(format);
    if (it != formats.end())
      result = it->second;

    it = formats.find(ALL_FORMATS_KEY);
    if (it != formats.end())
      result.insert(result.end(), it->second.begin(), it->second.end());

    return result;
  }

  static config parse(const char* filename)
  {
    config cfg;

    std::ifstream file(filename);

    if (!file.is_open())
      throw std::runtime_error("Could not open config file '" +
                               std::string(filename) + "'");

    std::string line;
    while (std::getline(file, line)) {
      line = remove_comment_and_trailing_whitespace(line);
      if (line.empty())
        continue;

      auto pos = line.find('=');
      if (pos == std::string::npos)
        throw std::runtime_error("Invalid config line");

      auto key = line.substr(0, pos);
      auto value = line.substr(pos + 1);

      try {
        if (key == "processing_timeout_secs") {
          cfg.processing_timeout_secs = std::stoi(value);
        } else if (key == "socket_kill_timeout_secs") {
          cfg.socket_kill_timeout_secs = std::stoi(value);
        } else if (key == "thread_pool_size") {
          cfg.thread_pool_size = std::stoi(value);
        } else if (key == "upload_limit") {
          cfg.upload_limit_bytes = parse_bytes(value);
        } else if (key == "sizes") {
          std::string::size_type start = 0;
          while (start < value.size()) {
            auto end = value.find(',', start);
            if (end == std::string::npos)
              end = value.size();
            cfg.sizes.push_back(
              size_spec::parse(value.substr(start, end - start)));
            start = end + 1;
          }
        } else if (key.length() > 8 && key.compare(0, 8, "formats.") == 0) {
          auto format = key.substr(8);
          std::vector<std::string> formats;
          std::string::size_type start = 0;
          while (start < value.size()) {
            auto end = value.find(',', start);
            if (end == std::string::npos)
              end = value.size();
            formats.push_back(value.substr(start, end - start));
            start = end + 1;
          }
          if (formats.empty())
            throw std::runtime_error("No formats specified for key: " + key);
          auto result = cfg.formats.emplace(format, std::move(formats));
          if (!result.second)
            throw std::runtime_error("Duplicate format key: " + key);
        } else {
          throw std::runtime_error("Unknown config key");
        }
      } catch (std::exception const& e) {
        throw std::runtime_error("Error parsing config key '" + key +
                                 "': " + e.what());
      }
    }

    if (cfg.sizes.empty())
      throw std::runtime_error("No sizes specified");
    if (cfg.formats.empty())
      throw std::runtime_error("No formats specified");

    if (cfg.socket_kill_timeout_secs <= cfg.processing_timeout_secs)
      throw std::runtime_error("socket_kill_timeout_secs must be greater than "
                               "processing_timeout_secs");

    if (cfg.processing_timeout_secs == 0)
      throw std::runtime_error(
        "processing_timeout_secs must be greater than 0");

    return cfg;
  }
};

#endif // CONFIG_HPP
