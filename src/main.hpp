#ifndef MAIN_HPP
#define MAIN_HPP

#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "../vendor/ada.hpp"

#include "image_processing.hpp"
#include "string_utils.hpp"
#include "thread_pool.hpp"

class http_connection;

class resize_executor
{
private:
  std::shared_ptr<http_connection> conn;
  worker_request_data data;

public:
  resize_executor(std::weak_ptr<http_connection> conn,
                  worker_request_data&& data)
    : conn(conn)
    , data(std::move(data))
  {
  }

  void perform_work();
};

class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
  using worker_pool_t = thread_pool<resize_executor>;

private:
  friend void resize_executor::perform_work();

  worker_pool_t& pool;
  boost::asio::ip::tcp::socket socket;
  // A buffer for performing reads.
  boost::beast::flat_buffer buffer{ 8192 };

  boost::beast::http::request_parser<boost::beast::http::dynamic_body>
    request_parser;
  boost::beast::http::response<boost::beast::http::dynamic_body> response;

  boost::asio::steady_timer deadline{ socket.get_executor(),
                                      std::chrono::seconds(8) };

  std::vector<std::shared_ptr<resize_executor>> my_executors;

  void respond_with_data(
    std::variant<processing_result, std::string> const& result)
  {
    if (std::holds_alternative<std::string>(result)) {
      response.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response.body())
        << "{\"error\": \""
        << json_sanitize_string(std::get<std::string>(result)) << "\"}";
    } else {
      std::get<processing_result>(result).write_json(
        boost::beast::ostream(response.body()));
    }

    write_response();
  }

  void read_request()
  {
    request_parser.body_limit(1024 * 1024 *
                              5); // TODO: make this limit configurable

    auto self = shared_from_this();

    boost::beast::http::async_read(
      socket, buffer, request_parser, [self](auto a, auto b) {
        self->check_read_error(a, b);
      });
  }

  void check_read_error(boost::beast::error_code ec,
                        std::size_t _bytes_transferred)
  {
    auto const& request = request_parser.get();
    response.version(request.version());
    response.keep_alive(false);
    response.set(boost::beast::http::field::content_type, "application/json");

    if (!ec) {
      process_request();
      return;
    }

    if (ec == boost::beast::http::error::body_limit) {
      response.result(boost::beast::http::status::payload_too_large);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.payload_too_large\"}";
    } else {
      response.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.server_error\"}";
    }

    write_response();
  }

  void process_request()
  {
    auto const& request = request_parser.get();
    auto url = ada::parse<ada::url>("http://example.com" +
                                    std::string(request.target()));
    if (!url) {
      response.result(boost::beast::http::status::internal_server_error);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.server_error\"}";
      write_response();
      return;
    }
    if (url->get_pathname() != "/api/upload") {
      response.result(boost::beast::http::status::not_found);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.not_found\"}";
      write_response();
      return;
    }
    if (request.method() != boost::beast::http::verb::post) {
      response.result(boost::beast::http::status::method_not_allowed);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.method_not_allowed\"}";
      write_response();
      return;
    }

    ada::url_search_params params(url->get_search());
    if (!params.has("filename")) {
      response.result(boost::beast::http::status::bad_request);
      boost::beast::ostream(response.body())
        << "{\"error\": \"error.missing_filename\"}";
      write_response();
      return;
    }
    // start image processing in the thread pool; the pool calls
    // respond_with_data when done with the task
    worker_request_data data{ request.body(),
                              std::string(params.get("filename").value()) };
    auto executor =
      std::make_shared<resize_executor>(weak_from_this(), std::move(data));
    my_executors.push_back(executor);
    pool.add_task(executor);
  }

  void write_response()
  {
    auto self = shared_from_this();

    response.content_length(response.body().size());

    boost::beast::http::async_write(
      socket, response, [self](boost::beast::error_code ec, std::size_t) {
        self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        self->deadline.cancel();
      });
  }

  void kill_on_deadline()
  {
    auto self = shared_from_this();

    deadline.async_wait([self](boost::beast::error_code ec) {
      if (!ec) {
        self->socket.close(ec);
        // TODO: cancel image processing here
      }
    });
  }

public:
  http_connection(boost::asio::ip::tcp::socket socket, worker_pool_t& pool)
    : socket(std::move(socket))
    , pool(pool)
  {
  }

  // Initiate the asynchronous operations associated with the connection.
  void start()
  {
    read_request();
    kill_on_deadline();
  }
};

void
resize_executor::perform_work()
{
  auto result = process_data(std::move(data));
  conn->respond_with_data(result);
}

#endif // MAIN_HPP