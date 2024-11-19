#ifndef STORAGE_INTERFACE_HPP
#define STORAGE_INTERFACE_HPP

#include <string_view>

class storage_interface
{
public:
  virtual ~storage_interface() = default;

  virtual void set_config(std::string_view const& key,
                          std::string_view const& value) = 0;

  virtual void validate() const = 0;

  virtual void init() = 0;
};

#endif // STORAGE_INTERFACE_HPP