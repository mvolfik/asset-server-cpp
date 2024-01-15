#include "main.hpp"

std::string
json_sanitize_string(std::string const& s)
{
  auto without_backslash = std::regex_replace(s, std::regex("\\\\"), "\\\\");
  return std::regex_replace(
    std::move(without_backslash), std::regex("\""), "\\\"");
}

std::string_view
remove_suffix(std::string_view s)
{
  auto pos = s.find_last_of('.');
  if (pos == std::string_view::npos || pos < s.length() - 5)
    return s;
  return s.substr(0, pos);
}

std::string
sanitize_filename(std::string_view const& s)
{
  std::string result;
  for (auto c : s) {
    if (std::isalnum(c) || c == '-' || c == '_')
      result += c;
    else
      result += '_';
  }
  return result;
}

std::variant<processing_result, std::string>
process_data(worker_data request)
{
  auto data = std::move(request.data);
  boost::beast::flat_buffer buffer;
  boost::asio::buffer_copy(buffer.prepare(data.size()), data.data());
  buffer.commit(data.size());

  Magick::Image image;
  try {
    image = (Magick::Blob(buffer.data().data(), buffer.data().size()));
  } catch (std::exception const& e) {
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

thread_pool::thread_pool(int n)
{
  for (int i = 0; i < n; i++) {
    threads.emplace_back([this] {
      while (true) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !tasks.empty(); });
        auto [data, conn] = tasks.front();
        tasks.pop_front();
        lock.unlock();
        conn->respond_with_data(process_data(data));
      }
    });
  }
}

void
http_server(boost::asio::ip::tcp::acceptor& acceptor,
            boost::asio::ip::tcp::socket& socket,
            thread_pool& pool)
{
  acceptor.async_accept(socket, [&](boost::beast::error_code ec) {
    // start the request, and "recurse" to accept next connection (it just
    // calls the function again, passing the references through, and the
    // previous call returns)
    if (!ec)
      std::make_shared<http_connection>(std::move(socket), pool)->start();
    http_server(acceptor, socket, pool);
  });
}

int
main(int argc, char* argv[])
{
  try {
    const char* ip = "0.0.0.0";
    unsigned short port = 8000;
    std::cerr << "Listening on http://" << ip << ":" << port << std::endl;

    boost::asio::io_context ctx;

    auto address = boost::asio::ip::make_address_v4(ip);
    boost::asio::ip::tcp::acceptor acceptor{ ctx, { address, port } };
    boost::asio::ip::tcp::socket socket{ ctx };

    thread_pool pool(2);
    http_server(acceptor, socket, pool);

    ctx.run();
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}