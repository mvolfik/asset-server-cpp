#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <fstream>
#include <optional>
#include <string>
#include <thread>

struct config
{
  unsigned processing_timeout_secs = 8;
  unsigned socket_kill_timeout_secs = 10;

  std::optional<unsigned> thread_pool_size;

  unsigned upload_limit_bytes = 20 * 1024 * 1024;

  unsigned get_thread_pool_size() const
  {
    return thread_pool_size.value_or(std::thread::hardware_concurrency() + 1);
  }
};

unsigned
parse_bytes(std::string const& s, std::string const& prop_name)
{
  unsigned val = 0;
  for (int i = 0; i < s.size(); i++) {
    if (s[i] < '0' || s[i] > '9') {
      if (i != s.size() - 1)
        throw std::runtime_error("Failed to parse value for " + prop_name +
                                 ": " + s);

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
          throw std::runtime_error("Failed to parse value for " + prop_name +
                                   ": " + s);
      }
      return val;
    }
    val = val * 10 + (s[i] - '0');
  }
  std::cerr
    << "Warning: no suffix for number in config file, assuming bytes for "
    << prop_name << std::endl;
  return val;
}

config
parse_config(const char* filename)
{
  config cfg;

  std::ifstream file(filename);

  if (!file.is_open())
    throw std::runtime_error("Could not open config file '" +
                             std::string(filename) + "'");

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    auto pos = line.find('=');
    if (pos == std::string::npos)
      throw std::runtime_error("Invalid config line");

    auto key = line.substr(0, pos);
    auto value = line.substr(pos + 1);

    if (key == "processing_timeout_secs") {
      cfg.processing_timeout_secs = std::stoi(value);
    } else if (key == "socket_kill_timeout_secs") {
      cfg.socket_kill_timeout_secs = std::stoi(value);
    } else if (key == "thread_pool_size") {
      cfg.thread_pool_size = std::stoi(value);
    } else if (key == "upload_limit") {
      cfg.upload_limit_bytes = parse_bytes(value, "upload_limit");
    } else {
      throw std::runtime_error("Unknown config key: " + key);
    }
  }

  if (cfg.socket_kill_timeout_secs <= cfg.processing_timeout_secs)
    throw std::runtime_error(
      "socket_kill_timeout_secs must be greater than processing_timeout_secs");

  if (cfg.processing_timeout_secs == 0)
    throw std::runtime_error("processing_timeout_secs must be greater than 0");

  return cfg;
}

#endif // CONFIG_HPP
