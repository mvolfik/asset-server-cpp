#ifndef IMAGE_PROCESSING_HPP
#define IMAGE_PROCESSING_HPP

#include <iostream>
#include <variant>
#include <vector>

#include <boost/beast/core/span.hpp>

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

struct processing_error_result
{
  std::string error;
  boost::beast::http::status response_code;
};

struct processing_empty_result{};

struct worker_request_data
{
  std::vector<std::uint8_t> data;
  std::string filename;
};


std::variant<processing_result, processing_error_result, processing_empty_result>
process_data(worker_request_data request, std::atomic_bool& cancelled_flag)
{
  if (cancelled_flag) {
    std::cerr << "Cancelled processing at start" << std::endl;
    return processing_error_result{
      "error.cancelled", boost::beast::http::status::service_unavailable
    };
  }

  Magick::Image image;
  try {
    image = (Magick::Blob(request.data.data(), request.data.size()));
  } catch (std::exception const& e) {
    std::cerr << "Error reading image: " << e.what() << std::endl;
    return processing_error_result{ "error.likely_corrupted_image",
                                    boost::beast::http::status::bad_request };
  }

  image.resize(Magick::Geometry(100, 100, 0, 0));

  if (cancelled_flag) {
    std::cerr << "Cancelled processing after resize" << std::endl;
    return processing_error_result{
      "error.cancelled", boost::beast::http::status::service_unavailable
    };
  }

  auto filename = sanitize_filename(remove_suffix(request.filename));

  try {
    image.write("data/" + filename + ".jpg");
    image.write("data/" + filename + ".webp");
  } catch (std::exception const& e) {
    std::cerr << "Error writing file: " << e.what() << std::endl;
    return processing_error_result{
      "error.error_writing_file",
      boost::beast::http::status::internal_server_error
    };
  }

  return processing_result{ filename, { { 100, 100 } }, { "jpg", "webp" } };
}

#endif // IMAGE_PROCESSING_HPP