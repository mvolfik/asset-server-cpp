#ifndef STORAGE_FS_HPP
#define STORAGE_FS_HPP

#include <filesystem>
#include <string>
#include <string_view>

#include "interface.hpp"

class storage_fs;

class fs_staged_folder : public staged_folder
{
  friend class storage_fs;

private:
  std::filesystem::path path;
  std::string final_name;

  void create_file(std::string_view name,
                   std::uint8_t const* data,
                   size_t size) override
  {
    std::filesystem::path full_path = path;
    full_path /= name;
    std::ofstream file(full_path, std::ios::binary);
    if (!file.is_open())
      throw std::runtime_error("Failed to open file " + full_path.string() +
                               " for writing");
    file.write(reinterpret_cast<char const*>(data), size);
  }

  void create_folder(std::string_view name) override
  {
    std::filesystem::path full_path = path;
    full_path /= name;
    std::filesystem::create_directory(full_path);
  }
};

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

  std::unique_ptr<staged_folder> create_staged_folder(
    std::string_view folder) override
  {

    std::filesystem::path full_path = temp_dir;
    full_path /= std::string(folder) + std::to_string(std::rand());
    std::cerr << "Creating staged folder at " << full_path << std::endl;

    std::filesystem::create_directory(full_path);
    auto result = std::make_unique<fs_staged_folder>();
    result->path = full_path;
    result->final_name = folder;
    return result;
  }

  void commit_staged_folder(std::unique_ptr<staged_folder> folder) override
  {
    auto fs_folder = dynamic_cast<fs_staged_folder*>(folder.get());
    if (!fs_folder)
      throw std::runtime_error("commit_staged_folder called with wrong type");
    std::filesystem::path full_path = data_dir;
    full_path /= fs_folder->final_name;
    std::filesystem::rename(fs_folder->path, full_path);
  }
};

#endif // STORAGE_FS_HPP