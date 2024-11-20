#ifndef IMAGE_PROCESSING_HPP
#define IMAGE_PROCESSING_HPP

#include <functional>
#include <iostream>

#include "server_state.hpp"
#include "thread_pool.hpp"
#include "utils.hpp"

struct dimensions_spec
{
  dimension_t width;
  dimension_t height;
  std::vector<std::string> formats;

  template<typename Stream>
  void write_json(Stream& stream) const
  {
    stream << "{\"width\": " << width << ", \"height\": " << height
           << ", \"formats\": [";
    bool first = true;
    for (auto const& f : formats) {
      if (!first)
        stream << ", ";
      first = false;
      stream << "\"" << f << "\"";
    }
    stream << "]}";
  }

  void width_height_from_string(std::string_view str)
  {
    auto pos = str.find('x');
    if (pos == std::string::npos) {
      throw std::invalid_argument("Invalid dimensions string: " +
                                  std::string(str));
    } else {
      width = string_view_to_int(str.substr(0, pos));
      height = string_view_to_int(str.substr(pos + 1));
    }
  }
};

/**
 *
 * @tparam ReadyHook A callback of type void(std::exception const* e) - called
 * when processing is done, with nullptr if everything went well, or a pointer
 * to the exception if an error occurred.
 */
template<typename ReadyHook>
class image_processor
{
private:
  task_group group;
  server_state state;
  ReadyHook ready_hook;

  std::shared_ptr<std::atomic<bool>> processing_flag;

  std::vector<dimensions_spec> dimensions;
  std::string hash;
  std::string filename;
  std::string original_format;

  void finalize(std::exception const* e)
  {
    if (processing_flag) {
      processing_flag->store(false);
      processing_flag->notify_all();
    }
    std::invoke(ready_hook, e);
  }

  bool find_existing_data(std::string const& hash)
  {
    auto folder = state.server_config.storage->walk_folder(hash);
    if (!folder)
      return false;

    std::string const* original = nullptr;
    for (auto const& entry : *folder) {
      if (entry.children)
        continue;

      if (original) {
        std::cerr << "Warning: multiple files found in root folder for hash "
                  << hash << ", using " << original << " as original"
                  << std::endl;
      }
      original = &entry.name;
    }
    if (!original)
      throw std::runtime_error("No original file found for hash " + hash);
    filename = get_filename_without_extension(*original);
    original_format = get_extension(*original);

    for (auto const& entry : *folder) {
      if (!entry.children)
        continue;

      dimensions_spec spec;
      spec.width_height_from_string(entry.name);
      for (auto const& format_entry : *entry.children) {
        if (get_filename_without_extension(format_entry.name) != filename)
          throw std::runtime_error("Filename mismatch in folder " + hash + "/" +
                                   entry.name + ": " + format_entry.name +
                                   " (expected " + filename + ")");
        spec.formats.push_back(std::string(get_extension(format_entry.name)));
      }
      dimensions.push_back(spec);
    }

    return true;
  }

  void start(std::vector<std::uint8_t> const& data)
  {
    hash = sha256(data);
    hash.resize(16);
    if (find_existing_data(hash))
      return;

    bool should_process_here;
    {
      std::lock_guard lock(state.currently_processing_mutex);

      auto result = state.currently_processing.emplace(
        hash, std::make_shared<std::atomic<bool>>(true));
      should_process_here = result.second;
      processing_flag = result.first->second;
    }
    if (!should_process_here) {
      processing_flag->wait(true);
      if (!find_existing_data(hash))
        throw std::runtime_error("Failed to find data after another thread "
                                 "reportedly finished processing");
      return;
    }
  }

public:
  image_processor(server_state state,
                  ReadyHook ready_hook,
                  std::vector<std::uint8_t> const& data,
                  std::string const& suggested_filename)
    : group(
        state.pool,
        [this](std::exception const& e) { finalize(&e); },
        std::bind(&image_processor::finalize, this, nullptr))
    , state(state)
    , ready_hook(ready_hook)
    , filename(
        sanitize_filename(get_filename_without_extension(suggested_filename)))
  {
    group.add_task(std::bind(&image_processor::start, this, data));
  }

  std::vector<dimensions_spec> const& get_dimensions() const
  {
    return dimensions;
  }

  std::string const& get_hash() const { return hash; }

  std::string const& get_filename() const { return filename; }

  std::string const& get_original_format() const { return original_format; }
};

#endif // IMAGE_PROCESSING_HPP