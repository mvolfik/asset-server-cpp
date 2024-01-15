#ifndef MAIN_HPP

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <Magick++.h>

#include "../vendor/ada.hpp"

#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <variant>

struct worker_data
{
  boost::beast::multi_buffer const& data;
  std::string_view filename;
};

std::string
json_sanitize_string(std::string const& s);

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

class http_connection;

class thread_pool
{
private:
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::tuple<worker_data, std::shared_ptr<http_connection>>> tasks;
  std::vector<std::thread>
    threads; // general TODO (not just here): do some proper shutdown on SIGINT
  boost::asio::io_context ctx;

public:
  thread_pool(int n);

  void add_task(worker_data data, std::shared_ptr<http_connection>&& conn)
  {
    std::unique_lock<std::mutex> lock(mutex);
    tasks.emplace_back(data, std::move(conn));
    cv.notify_one();
  }
};

class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
  http_connection(boost::asio::ip::tcp::socket socket, thread_pool& pool)
    : socket_(std::move(socket))
    , pool_(pool)
  {
  }

  // Initiate the asynchronous operations associated with the connection.
  void start()
  {
    read_request();
    kill_on_deadline();
  }

  void respond_with_data(
    std::variant<processing_result, std::string> const& result)
  {
    if (std::holds_alternative<std::string>(result)) {
      response_.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response_.body())
        << "{\"error\": \""
        << json_sanitize_string(std::get<std::string>(result)) << "\"}";
    } else {
      std::get<processing_result>(result).write_json(
        boost::beast::ostream(response_.body()));
    }

    write_response();
  }

private:
  thread_pool& pool_;
  boost::asio::ip::tcp::socket socket_;
  // A buffer for performing reads.
  boost::beast::flat_buffer buffer_{ 8192 };

  boost::beast::http::request_parser<boost::beast::http::dynamic_body>
    request_parser;
  boost::beast::http::response<boost::beast::http::dynamic_body> response_;

  boost::asio::steady_timer deadline_{ socket_.get_executor(),
                                       std::chrono::seconds(8) };

  void read_request()
  {
    request_parser.body_limit(1024 * 1024 * 5);

    auto self = shared_from_this();

    boost::beast::http::async_read(
      socket_, buffer_, request_parser, [self](auto a, auto b) {
        self->check_read_error(a, b);
      });
  }

  void check_read_error(boost::beast::error_code ec,
                        std::size_t _bytes_transferred)
  {
    auto const& request = request_parser.get();
    response_.version(request.version());
    response_.keep_alive(false);
    response_.set(boost::beast::http::field::content_type, "application/json");

    if (!ec) {
      process_request();
      return;
    }

    if (ec == boost::beast::http::error::body_limit) {
      response_.result(boost::beast::http::status::payload_too_large);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.payload_too_large\"}";
    } else {
      response_.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.server_error\"}";
    }

    write_response();
  }

  void process_request()
  {
    auto const& request = request_parser.get();
    auto url = ada::parse<ada::url>("http://example.com" + std::string(request.target()));
    if (!url) {
      response_.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.server_error\"}";
      write_response();
      return;
    }
    if (url->get_pathname() != "/api/upload") {
      response_.result(boost::beast::http::status::not_found);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.not_found\"}";
      write_response();
      return;
    }
    if (request.method() != boost::beast::http::verb::post) {
      response_.result(boost::beast::http::status::method_not_allowed);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.method_not_allowed\"}";
      write_response();
      return;
    }

    ada::url_search_params params(url->get_search());
    if (!params.has("filename")) {
      response_.result(boost::beast::http::status::bad_request);
      boost::beast::ostream(response_.body())
        << "{\"error\": \"error.missing_filename\"}";
      write_response();
      return;
    }
    // start image processing in the thread pool; the pool calls
    // respond_with_data when done with the task
    pool_.add_task({ request.body(), params.get("filename").value() },
                   shared_from_this());
  }

  void write_response()
  {
    auto self = shared_from_this();

    response_.content_length(response_.body().size());

    boost::beast::http::async_write(
      socket_, response_, [self](boost::beast::error_code ec, std::size_t) {
        self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        self->deadline_.cancel();
      });
  }

  void kill_on_deadline()
  {
    auto self = shared_from_this();

    deadline_.async_wait([self](boost::beast::error_code ec) {
      if (!ec) {
        self->socket_.close(ec);
        // TODO: cancel image processing here
      }
    });
  }
};

#endif // MAIN_HPP