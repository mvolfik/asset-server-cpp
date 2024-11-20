#ifndef STORAGE_FS_HPP
#define STORAGE_FS_HPP

#include <filesystem>
#include <string>
#include <string_view>

#include "interface.hpp"

class storage_fs : public storage_interface
{
private:
  std::string data_dir;
  std::string temp_dir;

public:
  void set_config(std::string_view key, std::string_view value) override
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

  std::optional<std::vector<folder_entry>> walk_folder(
    std::string_view path) const override
  {
    std::filesystem::path full_path = data_dir;
    full_path /= path;
    if (!std::filesystem::exists(full_path))
      return std::nullopt;

    std::vector<folder_entry> result;
    for (auto const& entry : std::filesystem::directory_iterator(full_path)) {
      folder_entry folder;
      folder.name = entry.path().filename().string();
      if (entry.is_directory()) {
        // take the full path, then remove the data_dir prefix, which will be
        // added in the recursive call
        auto subdir_path_datadir_relative =
          entry.path().string().substr(data_dir.size() + 1);

        folder.children = walk_folder(subdir_path_datadir_relative);
        if (!folder.children)
          throw std::runtime_error("walk_folder found a folder at " +
                                   entry.path().relative_path().string() +
                                   ", but recursive call returned nullopt");
      }
      result.push_back(folder);
    }
    return result;
  }
};

#endif // STORAGE_FS_HPP