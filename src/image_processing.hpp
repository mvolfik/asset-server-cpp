#ifndef IMAGE_PROCESSING_HPP
#define IMAGE_PROCESSING_HPP

#include <functional>
#include <iostream>
#include <string_view>

using namespace std::string_view_literals;

#include <magic.h>
#include <vips/vips8>

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

void
init_image_processing(server_state& state)
{
  if (VIPS_INIT("asset-server")) {
    throw std::runtime_error("Failed to initialize libvips");
  }

  state.magic_cookie = magic_open(MAGIC_EXTENSION);
  int result = magic_load(state.magic_cookie, nullptr);
  if (result != 0)
    throw std::runtime_error("Failed to load default libmagic database");
}

class image_processor : public std::enable_shared_from_this<image_processor>
{
private:
  using ReadyHook = std::function<void(std::exception const*)>;

  task_group group;
  server_state state;
  ReadyHook ready_hook;

  std::shared_ptr<std::atomic<bool>> processing_flag;
  std::unique_ptr<staged_folder> temp_folder;

  std::vector<dimensions_spec> dimensions;
  std::string hash;
  std::string filename;
  dimensions_spec
    original; // the vector of formats MUST contain exactly one item
  bool is_new = false;

  /**
   * This is called as a callback when task_group is done
   */
  void finalize(std::exception const* e)
  {
    if (processing_flag) {
      // here you can add any work that needs to be done after all files are
      // ready in the staged folder
      if (!e) {
        state.server_config.storage->commit_staged_folder(
          std::move(temp_folder));
      } // TODO: we don't free the staged folder if error happens

      {
        std::lock_guard lock(state.currently_processing_mutex);
        state.currently_processing.erase(hash);
      }
      processing_flag->store(false);
      processing_flag->notify_all();
    }
    std::invoke(ready_hook, e);
  }

  void save_to_format(std::shared_ptr<vips::VImage> img,
                      unsigned dimension_index,
                      unsigned format_index)
  {
    auto& spec = dimensions[dimension_index];
    auto& format = spec.formats[format_index];

    std::uint8_t* buffer;
    size_t size;
    std::cerr << "Saving " << spec.width << "x" << spec.height << " to "
              << format << std::endl;
    img->write_to_buffer(("." + format).c_str(), (void**)&buffer, &size);

    temp_folder->create_file(std::to_string(spec.width) + "ax" +
                               std::to_string(spec.height) + "/" + filename +
                               "." + format,
                             buffer,
                             size);
  }

  void resize(std::shared_ptr<vips::VImage> img, unsigned index)
  {
    auto& spec = dimensions[index];

    std::cerr << "Resizing " << index << " to " << spec.width << "x"
              << spec.height << std::endl;
    auto resized =
      std::make_shared<vips::VImage>(img->thumbnail_image(spec.width));
    spec.height = resized->height();

    temp_folder->create_folder(std::to_string(spec.width) + "x" +
                               std::to_string(spec.height));

    for (auto&& format : state.server_config.get_formats(original.formats[0])) {
      spec.formats.push_back(std::move(format));
    }

    for (unsigned i = 0; i < spec.formats.size(); ++i) {
      group.add_task_from_weak(this->weak_from_this(),
                               [resized, index, i](image_processor& self) {
                                 self.save_to_format(resized, index, i);
                               });
    }
  }

  void start_processing(std::vector<std::uint8_t> const& data)
  {
    temp_folder = state.server_config.storage->create_staged_folder(hash);

    auto magic_format =
      magic_buffer(state.magic_cookie, data.data(), data.size());
    if (!magic_format || std::string_view(magic_format) == "???"sv) {
      std::cerr << "Failed to determine correct original format of image, "
                   "trusting the uploader"
                << std::endl;
    } else {
      original.formats[0] = magic_format;
    }

    temp_folder->create_file(
      filename + "." + original.formats[0], data.data(), data.size());

    auto image = std::make_shared<vips::VImage>(
      vips::VImage::new_from_buffer(data.data(), data.size(), nullptr));
    original.width = image->width();
    original.height = image->height();

    // we won't need any lock on the dimensions array: we push all the elements
    // here, and then each parallel resize task will only write to its own
    // element
    for (auto const& size : state.server_config.get_sizes(original.width)) {
      dimensions_spec spec;
      spec.width = size;
      spec.height = 0; // will be set by resize()
      dimensions.push_back(spec);
    }

    for (unsigned i = 0; i < dimensions.size(); ++i) {
      group.add_task_from_weak(
        this->weak_from_this(),
        [image, i](image_processor& self) { self.resize(image, i); });
    }
  }

  bool find_existing_data(std::string const& hash)
  {
    auto folder = state.server_config.storage->walk_folder(hash);
    if (!folder)
      return false;

    std::string const* original_filename = nullptr;
    for (auto const& entry : *folder) {
      if (entry.children)
        continue;

      if (original_filename) {
        std::cerr << "Warning: multiple files found in root folder for hash "
                  << hash << ", using " << original_filename << " as original"
                  << std::endl;
      }
      original_filename = &entry.name;
    }
    if (!original_filename)
      throw std::runtime_error("No original file found for hash " + hash);
    filename = get_filename_without_extension(*original_filename);
    // TODO: here we don't load the dimensions of the original and simply return
    // 0. Getting the current dimensions requires loading the image, which I'm
    // not sure is worth it.
    original = { 0, 0, { std::string(get_extension(*original_filename)) } };

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
      dimensions.push_back(std::move(spec));
    }

    return true;
  }

  void check_existence(std::vector<std::uint8_t> const& data)
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

    if (find_existing_data(hash)) {
      std::cerr << "Wow! The rare scenario happened: another thread finished "
                   "processing while we were waiting for the lock"
                << std::endl;
      return;
    }

    is_new = true;

    group.add_task_from_weak(
      this->weak_from_this(),
      [&data](image_processor& self) { self.start_processing(data); });

    // add here any other tasks that can be done in parallel to image
    // processing. Note that at this point, the original dimensions_spec and the
    // dimensions vector are empty.
  }

  struct PrivateTag
  {
    explicit PrivateTag() = default;
  };

public:
  /// Constructor only for use by create()
  image_processor(PrivateTag,
                  server_state state,
                  ReadyHook&& ready_hook,
                  std::vector<std::uint8_t> const& data,
                  std::string const& suggested_filename)
    : group(
        state.pool,
        [this](std::exception const& e) { finalize(&e); },
        [this]() { finalize(nullptr); })
    , state(state)
    , ready_hook(std::move(ready_hook))
    , filename(
        sanitize_filename(get_filename_without_extension(suggested_filename)))
    , original{ 0, 0, { sanitize_filename(get_extension(suggested_filename)) } }
  {
  }

  ~image_processor() {
    std::cerr << "Destroying image processor for " << hash << std::endl;
  }

  /**
   * Creates a new image processor, returning a shared pointer to it, and starts processing.
   * 
   * Since this class starts background tasks, it needs to ensure that it is not destroyed
   */
  static std::shared_ptr<image_processor> create(
    server_state state,
    ReadyHook&& ready_hook,
    std::vector<std::uint8_t> const& data,
    std::string const& suggested_filename)
  {
    if (!state.magic_cookie) {
      throw std::runtime_error("Image processing not initialized");
    }

    auto shared = std::make_shared<image_processor>(
      PrivateTag{}, state, std::move(ready_hook), data, suggested_filename);

    shared->group.add_task_from_weak(
      shared->weak_from_this(),
      [&data](image_processor& self) { self.check_existence(data); });

    return shared;
  }

  std::vector<dimensions_spec> const& get_dimensions() const
  {
    return dimensions;
  }

  std::string const& get_hash() const { return hash; }

  std::string const& get_filename() const { return filename; }

  dimensions_spec const& get_original() const { return original; }

  bool get_is_new() const { return is_new; }

  std::weak_ptr<image_processor> get_weak() { return this->weak_from_this(); }
};

#endif // IMAGE_PROCESSING_HPP