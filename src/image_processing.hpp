#ifndef IMAGE_PROCESSING_HPP
#define IMAGE_PROCESSING_HPP

#include <iostream>
#include <variant>
#include <vector>

#include <boost/beast/core/span.hpp>

#include <Magick++.h>

#include "config.hpp"
#include "string_utils.hpp"

struct dimensions_spec
{
  dimension_t width;
  dimension_t height;

  template<typename Stream>
  void write_json(Stream& stream) const
  {
    stream << "{\"width\": " << width << ", \"height\": " << height << "}";
  }
};

struct image_metadata
{
  /**
   * The filename is already sanitized to be only [A-Za-z0-9-_], therefore it is
   * safe to write to JSON
   */
  std::string filename;
  dimensions_spec original_dimensions;
  std::string original_format;

  std::vector<dimensions_spec> dimensions;
  std::vector<std::string> formats;

  template<typename Stream>
  void write_json(Stream& stream) const
  {
    stream << "{\"filename\": \"" << filename
           << "\", \"original_dimensions\": ";
    original_dimensions.write_json(stream);
    stream << ", \"original_format\": \"" << original_format
           << "\", \"dimensions\": [";

    bool first = true;
    for (auto const& d : dimensions) {
      if (!first)
        stream << ", ";
      first = false;

      d.write_json(stream);
    }

    stream << "], \"formats\": [";

    first = true;
    for (auto const& f : formats) {
      if (!first)
        stream << ", ";
      first = false;
      stream << "\"" << f << "\"";
    }

    stream << "]}";
  }
};

struct load_image_result
{
  image_metadata metadata;
  Magick::Image image;
};

struct error_result
{
  std::string error;
  boost::beast::http::status response_code;
};

struct empty_result
{};

using worker_result_variant =
  std::variant<load_image_result, error_result, empty_result>;

struct load_image_task_data
{
  std::vector<std::uint8_t> data;
  std::string filename;
};

struct resize_to_spec_task_data
{
  Magick::Image image;
  dimensions_spec spec;
  image_metadata const& metadata;
};

using worker_task_data =
  std::variant<load_image_task_data, resize_to_spec_task_data>;

worker_result_variant
load_image(load_image_task_data const& request, config const& config)
{
  Magick::Image image;
  try {
    image = (Magick::Blob(request.data.data(), request.data.size()));
  } catch (std::exception const& e) {
    std::cerr << "Error reading image: " << e.what() << std::endl;
    return error_result{ "error.likely_corrupted_image",
                         boost::beast::http::status::bad_request };
  }

  auto original_size = image.size();
  auto filename = sanitize_filename(remove_suffix(request.filename));

  std::set<dimension_t> widths = config.get_sizes(original_size.width());
  std::vector<dimensions_spec> dimensions(widths.size());

  std::size_t i = 0;
  for (auto width : widths) {
    dimensions[i] = { width,
                      div_round_close(width * original_size.height(),
                                      original_size.width()) };
    i++;
  }

  return load_image_result{
    .metadata =
      image_metadata{
        .filename = filename,
        .original_dimensions = { original_size.width(),
                                 original_size.height() },
        .original_format = "jpg",
        .dimensions = dimensions,
        .formats = { "jpg", "webp" } }, // TODO: make the formats configurable
    .image = std::move(image)
  };
}

worker_result_variant
resize_to_spec(resize_to_spec_task_data const& request)
{
  auto image = request.image;
  Magick::Geometry geo(request.spec.width, request.spec.height, 0, 0);
  geo.aspect(true);
  image.resize(geo);
  auto fn_base = "data/" + request.metadata.filename + "_" +
                 std::to_string(request.spec.width) + "x" +
                 std::to_string(request.spec.height);
  try {
    image.write(fn_base + ".jpg");
    image.write(fn_base + ".webp");
  } catch (std::exception const& e) {
    std::cerr << "Error writing file: " << e.what() << std::endl;
    return error_result{ "error.error_writing_file",
                         boost::beast::http::status::internal_server_error };
  }

  return empty_result{};
}

#endif // IMAGE_PROCESSING_HPP