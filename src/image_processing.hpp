#ifndef IMAGE_PROCESSING_HPP
#define IMAGE_PROCESSING_HPP

#include <iostream>
#include <variant>

#include <Magick++.h>

#include "string_utils.hpp"

struct dimensions_spec
{
  int width;
  int height;
};

struct processing_result
{
  std::string filename;
  std::vector<dimensions_spec> dimensions;
  std::vector<std::string> formats;

  template<typename Stream>
  void write_json(Stream stream) const
  {
    stream << "{\"filename\": \"";
    stream << json_sanitize_string(filename);
    stream << "\", \"dimensions\": [";

    bool first = true;
    for (auto const& d : dimensions) {
      if (!first)
        stream << ", ";
      first = false;
      stream << "{\"width\": " << d.width << ", \"height\": " << d.height
             << "}";
    }

    stream << "], \"formats\": [";

    first = true;
    for (auto const& f : formats) {
      if (!first)
        stream << ", ";
      first = false;
      stream << "\"" << json_sanitize_string(f) << "\"";
    }

    stream << "]}";
  }
};

struct worker_request_data
{
  boost::beast::multi_buffer const& data;
  std::string filename;
};

std::variant<processing_result, std::string>
process_data(worker_request_data request)
{
  auto data = std::move(request.data);
  boost::beast::flat_buffer buffer;
  boost::asio::buffer_copy(buffer.prepare(data.size()), data.data());
  buffer.commit(data.size());

  Magick::Image image;
  try {
    image = (Magick::Blob(buffer.data().data(), buffer.data().size()));
  } catch (std::exception const& e) {
    std::cerr << "Error reading image: " << e.what() << std::endl;
    return "error.likely_corrupted_image";
  }

  image.resize(Magick::Geometry(100, 100, 0, 0));
  auto filename = sanitize_filename(remove_suffix(request.filename));

  try {
    image.write("data/" + filename + ".jpg");
    image.write("data/" + filename + ".webp");
  } catch (std::exception const& e) {
    std::cerr << "Error writing file: " << e.what() << std::endl;
    return "error.server_error";
  }

  return processing_result{ filename, { { 100, 100 } }, { "jpg", "webp" } };
}

#endif // IMAGE_PROCESSING_HPP