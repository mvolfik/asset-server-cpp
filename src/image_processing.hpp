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

class image_loading_error : public std::runtime_error
{
public:
  image_loading_error()
    : std::runtime_error("")
  {
  }
};

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

void
destroy_image_processing(server_state& state)
{
  magic_close(state.magic_cookie);
  vips_shutdown();
}

class image_processor : public std::enable_shared_from_this<image_processor>
{
private:
  using ReadyHook = std::function<void(std::exception const*,
                                       std::shared_ptr<image_processor>)>;

  task_group group;
  server_state state;
  ReadyHook ready_hook;

  /**
   * Notifier from the server's hashmap, either to sleep on, or to notify
   * others waiting for this image
   */
  std::shared_ptr<std::atomic<bool>> processing_done_notifier;
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
    if (processing_done_notifier) {
      if (!e) {
        state.server_config.storage->commit_staged_folder(*temp_folder);
      }

      {
        std::lock_guard lock(state.currently_processing_mutex);
        state.currently_processing.erase(hash);
      }
      processing_done_notifier->store(false);
      processing_done_notifier->notify_all();
    }
    // here you can add any work that needs to be done after all files are
    // ready in the staged folder

    std::invoke(ready_hook, e, shared_from_this());
  }

  void save_to_format(std::shared_ptr<vips::VImage> img,
                      unsigned dimension_index,
                      unsigned format_index)
  {
    auto& spec = dimensions[dimension_index];
    auto& format = spec.formats[format_index];

    std::uint8_t* buffer;
    size_t size;
    img->write_to_buffer(("." + format).c_str(), (void**)&buffer, &size);

    temp_folder->create_file(std::to_string(spec.width) + "x" +
                               std::to_string(spec.height) + "/" + filename +
                               "." + format,
                             buffer,
                             size);
    g_free(buffer);
  }

  void resize(std::shared_ptr<vips::VImage> img, unsigned index)
  {
    auto& spec = dimensions[index];

    auto resized =
      std::make_shared<vips::VImage>(img->thumbnail_image(spec.width));
    spec.height = resized->height();

    temp_folder->create_folder(std::to_string(spec.width) + "x" +
                               std::to_string(spec.height));

    for (auto&& format : state.server_config.get_formats(original.formats[0])) {
      spec.formats.push_back(std::move(format));
    }

    auto self = shared_from_this();
    for (unsigned i = 0; i < spec.formats.size(); ++i) {
      group.add_task([resized, index, i, self]() {
        self->save_to_format(resized, index, i);
      });
    }
  }

  /**
   * Start processing after we've determined that this image is new, and any
   * necessary synchronization was set up.
   */
  void load_image(std::vector<std::uint8_t> const& data)
  {
    temp_folder = state.server_config.storage->create_staged_folder(hash);

    auto magic_format =
      magic_buffer(state.magic_cookie, data.data(), data.size());
    if (!magic_format || std::string_view(magic_format) == "???"sv) {
      std::cerr << "Failed to determine correct original format of image, "
                   "trusting the uploader"
                << std::endl;
    } else {
      std::size_t first_noninclusive = 0;
      while (magic_format[first_noninclusive] != '/' &&
             magic_format[first_noninclusive] != '\0') {
        ++first_noninclusive;
      }
      original.formats[0] = std::string(magic_format, first_noninclusive);
    }

    temp_folder->create_file(
      filename + "." + original.formats[0], data.data(), data.size());

    std::shared_ptr<vips::VImage> image;
    try {
      image = std::make_shared<vips::VImage>(
        vips::VImage::new_from_buffer(data.data(), data.size(), nullptr));
    } catch (vips::VError const& e) {
      std::cerr << "Failed to load image: " << e.what() << std::endl;
      throw image_loading_error();
    }
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

    auto self = shared_from_this();
    for (unsigned i = 0; i < dimensions.size(); ++i) {
      group.add_task([image, i, self]() { self->resize(image, i); });
    }
  }

  /**
   * Tries to find a folder with existing data for the given image hash.
   *
   * If none exists, returns false.
   *
   * If such folder exists, fills data members of this class with data
   * discovered from the folder, so that the image processor can be used
   * to send a response, and returns true.
   */
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
    // TODO: here we don't load the dimensions of the original and simply
    // return a zero. Getting the correct dimensions requires loading the
    // image, which I'm not sure is worth it. This will need an addition
    // to the API to read a file.
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
      std::sort(spec.formats.begin(), spec.formats.end());
      dimensions.push_back(std::move(spec));
    }

    return true;
  }

  void check_existence(std::vector<std::uint8_t> const& data)
  {
    hash = sha256(data);
    hash.resize(16);
    if (find_existing_data(hash))
      // existing data was found and filled into fields of this class, this task
      // can end, which will cause the task_group to finish and call finalize()
      return;

    bool should_process_here;

    {
      std::lock_guard lock(state.currently_processing_mutex);

      auto result = state.currently_processing.emplace(
        hash, std::make_shared<std::atomic<bool>>(true));
      should_process_here = result.second;
      processing_done_notifier = result.first->second;
    }

    if (!should_process_here) {
      processing_done_notifier->wait(true);
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

    group.add_task(
      [&data, self = shared_from_this()]() { self->load_image(data); });

    // add here any other tasks that can be done in parallel to image
    // processing. Note that at this point, the original dimensions_spec and the
    // dimensions vector are empty.
  }

  struct PrivateTag
  {
    explicit PrivateTag() = default;
  };

public:
  /// Constructor only for use by run()
  /// See the example on
  /// https://en.cppreference.com/w/cpp/memory/enable_shared_from_this
  /// - the constructor must be public, so that std::make_shared can create the
  /// object, but we use the PrivateTag, so that you can only really instantiate
  /// it through run()
  image_processor(PrivateTag,
                  server_state state,
                  ReadyHook&& ready_hook,
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

  /**
   * Runs the image processing pipeline. When everything is done, ready_hook is
   * called with a pointer to the processor, which you can use to read metadata
   * about the created files.
   *
   * Since this class starts background tasks, it needs to ensure that it is not
   * destroyed, so it manages itself inside through shared_ptr, and passes this
   * ptr to the callback.
   */
  static void run(server_state state,
                  ReadyHook&& ready_hook,
                  std::vector<std::uint8_t> const& data,
                  std::string const& suggested_filename)
  {
    if (!state.magic_cookie) {
      throw std::runtime_error("Image processing not initialized");
    }

    auto shared = std::make_shared<image_processor>(
      PrivateTag{}, state, std::move(ready_hook), suggested_filename);

    shared->group.add_task(
      [&data, shared]() { shared->check_existence(data); });
  }

  void cancel() { group.cancel(); }

  std::vector<dimensions_spec> const& get_dimensions() const
  {
    return dimensions;
  }

  std::string const& get_hash() const { return hash; }

  std::string const& get_filename() const { return filename; }

  dimensions_spec const& get_original() const { return original; }

  bool get_is_new() const { return is_new; }

  void write_result_json(std::ostream& stream) const
  {
    stream << "{\"hash\": \"" << hash << "\", \"filename\": \"" << filename
           << "\", \"original\": ";
    original.write_json(stream);
    stream << ", \"variants\": [";
    bool first = true;
    for (auto const& d : dimensions) {
      if (!first)
        stream << ", ";
      first = false;
      d.write_json(stream);
    }
    stream << "]}";
  }
};

#endif // IMAGE_PROCESSING_HPP