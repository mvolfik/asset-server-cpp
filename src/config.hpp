#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils.hpp"

#include "storage/fs.hpp"
#include "storage/interface.hpp"

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

  /**
   * Parse a size spec from a string.
   */
  static size_spec parse(std::string_view s)
  {
    size_spec spec;
    auto colon_pos = s.find(':');
    if (colon_pos == std::string::npos) {
      spec.fixed_value = string_view_to_int(s);
      spec.decrement = 0;
      spec.decrement_is_pct = false;
    } else {
      spec.fixed_value = string_view_to_int(s.substr(0, colon_pos));
      auto decrement_str = s.substr(colon_pos + 1);
      if (decrement_str.back() == '%') {
        spec.decrement_is_pct = true;
        decrement_str = decrement_str.substr(0, decrement_str.size() - 1);
      } else {
        auto back = decrement_str.back();
        if (back != 'x' && --back != 'p')
          throw std::runtime_error(
            "Invalid size spec, expected 'px' or '%' after number: " +
            std::string(s));

        spec.decrement_is_pct = false;
        decrement_str = decrement_str.substr(0, decrement_str.size() - 2);
      }
      spec.decrement = string_view_to_int(decrement_str);
      if (spec.decrement_is_pct && spec.decrement >= 100)
        throw std::runtime_error(
          "Percentual decrement must be smaller than 100: " + std::string(s));
      if (spec.decrement == 0)
        throw std::runtime_error("Decrement must be greater than 0: " +
                                 std::string(s));
    }
    return spec;
  }

  /**
   * Generate all sizes that this size spec means for an image with a concrete
   * width, and add them to the result set.
   */
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

struct size_specs
{
  std::vector<size_spec> specs;

  static size_specs parse(std::string_view s)
  {
    size_specs result;
    std::string::size_type start = 0;
    while (start < s.size()) {
      auto end = s.find(',', start);
      if (end == std::string::npos)
        end = s.size();
      result.specs.push_back(size_spec::parse(s.substr(start, end - start)));
      start = end + 1;
    }
    return result;
  }

  std::set<dimension_t> get_sizes(dimension_t original_width) const
  {
    std::set<dimension_t> result;

    for (auto const& spec : specs) {
      spec.get_sizes(original_width, result);
    }
    return result;
  }
};

/**
 * Parse a (byte) value from a number with an optional suffix (k, M, G).
 * Suffixes are interpreted as powers of 1024.
 */
unsigned
parse_bytes(std::string_view s)
{
  unsigned val = 0;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] < '0' || s[i] > '9') {
      if (i != s.size() - 1)
        throw std::runtime_error("Failed to parse value: " + std::string(s));

      switch (s[i]) {
        case 'B':
          break;
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
          throw std::runtime_error("Invalid byte value suffix: " +
                                   std::string(s));
      }
      return val;
    }
    val = val * 10 + (s[i] - '0');
  }
  throw std::runtime_error(
    "Missing byte value suffix (use 'B' to mark individual bytes): " +
    std::string(s));
  return val;
}

struct config
{
  std::string listen_host = "127.0.0.1";
  unsigned short listen_port = 8000;

  unsigned processing_timeout_secs = 8;
  unsigned socket_kill_timeout_secs = 10;

  std::optional<unsigned> thread_pool_size;

  unsigned upload_limit_bytes = 20 * 1024 * 1024;

  size_specs sizes;

  std::unordered_map<std::string, std::vector<std::string>> formats;
  static constexpr const char* ALL_FORMATS_KEY = "*";

  std::string auth_header_val;

  std::unique_ptr<storage_backend> storage = nullptr;

  unsigned get_thread_pool_size() const
  {
    if (!thread_pool_size)
      return std::thread::hardware_concurrency() + 1;
    return *thread_pool_size;
  }

  std::set<dimension_t> get_sizes(dimension_t original_width) const
  {
    return sizes.get_sizes(original_width);
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

    std::string buf;

    std::unordered_set<std::string> seen_keys;
    while (std::getline(file, buf)) {
      auto line = remove_comment_and_trailing_whitespace(buf);
      if (line.empty())
        continue;

      auto pos = line.find('=');
      if (pos == std::string::npos)
        throw std::runtime_error("Invalid config line");

      auto key = line.substr(0, pos);
      auto value = line.substr(pos + 1);

      if (!seen_keys.emplace(key).second)
        throw std::runtime_error("Duplicate config key: " + std::string(key));

      try {
        if (key == "listen_host") {
          cfg.listen_host = value;
        } else if (key == "listen_port") {
          cfg.listen_port = string_view_to_int(value);
        } else if (key == "processing_timeout_secs") {
          cfg.processing_timeout_secs = string_view_to_int(value);
        } else if (key == "socket_kill_timeout_secs") {
          cfg.socket_kill_timeout_secs = string_view_to_int(value);
        } else if (key == "thread_pool_size") {
          cfg.thread_pool_size = string_view_to_int(value);
        } else if (key == "upload_limit") {
          cfg.upload_limit_bytes = parse_bytes(value);
        } else if (key == "auth_token") {
          cfg.auth_header_val = "Bearer " + std::string(value);
        } else if (key == "sizes") {
          cfg.sizes = size_specs::parse(value);
        } else if (key == "storage.type") {
          if (value == "fs")
            cfg.storage = std::make_unique<storage_fs>();
          else
            throw std::runtime_error("Unknown storage type: " +
                                     std::string(value));
        } else if (key.substr(0, 8) == "storage.") {
          if (!cfg.storage)
            throw std::runtime_error("storage.type not specified (it must come "
                                     "before other storage.* keys)");

          cfg.storage->set_config(key.substr(8), value);
        } else if (key.substr(0, 8) == "formats.") {
          auto format = key.substr(8);
          std::vector<std::string> formats;
          std::string::size_type start = 0;
          while (start < value.size()) {
            auto end = value.find(',', start);
            if (end == std::string::npos)
              end = value.size();
            formats.push_back(std::string(value.substr(start, end - start)));
            start = end + 1;
          }
          if (formats.empty())
            throw std::runtime_error("No formats specified for key: " +
                                     std::string(key));
          auto result = cfg.formats.emplace(format, std::move(formats));
          if (!result.second)
            throw std::runtime_error("Duplicate format key: " +
                                     std::string(key));
        } else {
          throw std::runtime_error("Unknown config key");
        }
      } catch (std::exception const& e) {
        throw std::runtime_error("Error parsing config key '" +
                                 std::string(key) + "': " + e.what());
      }
    }

    if (cfg.sizes.specs.empty())
      throw std::runtime_error("No sizes specified");
    if (cfg.formats.empty())
      throw std::runtime_error("No formats specified");

    if (!cfg.storage)
      throw std::runtime_error("No storage type specified");

    cfg.storage->validate();

    if (cfg.socket_kill_timeout_secs <= cfg.processing_timeout_secs)
      throw std::runtime_error("socket_kill_timeout_secs must be greater than "
                               "processing_timeout_secs");

    if (cfg.processing_timeout_secs == 0)
      throw std::runtime_error(
        "processing_timeout_secs must be greater than 0");

    if (cfg.auth_header_val.empty())
      std::cerr << "Warning: no auth_token specified, server will be open for "
                   "uploads to anyone"
                << std::endl;

    return cfg;
  }
};

#endif // CONFIG_HPP
