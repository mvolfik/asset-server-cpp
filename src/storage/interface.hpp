#ifndef STORAGE_INTERFACE_HPP
#define STORAGE_INTERFACE_HPP

#include <string_view>

struct folder_entry
{
  std::string name;
  std::optional<std::vector<folder_entry>> children; // nullopt if it's a file
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
};

#endif // STORAGE_INTERFACE_HPP