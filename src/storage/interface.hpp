#ifndef STORAGE_INTERFACE_HPP
#define STORAGE_INTERFACE_HPP

#include <string_view>

struct folder_entry
{
  std::string name;
  std::optional<std::vector<folder_entry>> children; // nullopt if it's a file
};

class staged_folder
{
public:
  virtual ~staged_folder() = default;

  // while using a vector would be cleaner, vips returns a pointer+size, which
  // is practically impossible to convert to a vector without copying
  // https://stackoverflow.com/a/15203325/7292139
  virtual void create_file(std::string_view name,
                           std::uint8_t const* data,
                           size_t size) = 0;

  virtual void create_folder(std::string_view name) = 0;
};

class storage_interface
{
public:
  virtual ~storage_interface() = default;

  virtual void set_config(std::string_view key, std::string_view value) = 0;

  virtual void validate() const = 0;

  virtual void init() = 0;

  virtual std::optional<std::vector<folder_entry>> walk_folder(
    std::string_view path) const = 0;

  virtual std::unique_ptr<staged_folder> create_staged_folder(
    std::string_view folder) = 0;

  virtual void commit_staged_folder(std::unique_ptr<staged_folder> folder) = 0;
};

#endif // STORAGE_INTERFACE_HPP
