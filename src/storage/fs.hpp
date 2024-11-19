#ifndef STORAGE_FS_HPP
#define STORAGE_FS_HPP

#include <string>
#include <string_view>
#include <filesystem>

#include "interface.hpp"

class storage_fs : public storage_interface
{
private:
  std::string data_dir;
  std::string temp_dir;

public:
  void set_config(std::string_view const& key,
                  std::string_view const& value) override
  {
    if (key == "data_dir")
      data_dir = value;
    else if (key == "temp_dir")
      temp_dir = value;
    else
      throw std::runtime_error("Unknown storage config key: " +
                               std::string(key));
  }

  void validate() const override
  {
    if (data_dir.empty())
      throw std::runtime_error("data_dir not specified");
    if (temp_dir.empty())
      throw std::runtime_error("temp_dir not specified");
  }

  void init() override
  {
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directory(temp_dir);
    std::filesystem::create_directory(data_dir);
  }
};

#endif // STORAGE_FS_HPP