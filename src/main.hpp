#ifndef MAIN_HPP
#define MAIN_HPP

#include <atomic>
#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "../vendor/ada.hpp"

#include "config.hpp"
#include "image_processing.hpp"
#include "string_utils.hpp"
#include "thread_pool.hpp"

class http_connection;

class resize_executor
{
private:
  std::weak_ptr<http_connection> conn;
  worker_request_data data;
  std::atomic_bool cancelled{ false };

public:
  resize_executor(std::weak_ptr<http_connection> conn,
                  worker_request_data&& data)
    : conn(conn)
    , data(std::move(data))
  {
  }

  void perform_work();
  void cancel() { cancelled = true; }
};

class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
  using worker_pool_t = thread_pool<resize_executor>;

private:
  friend void resize_executor::perform_work();

  worker_pool_t& pool;
  config const& cfg;
  boost::asio::ip::tcp::socket socket;
  // A buffer for performing reads.
  boost::beast::flat_buffer buffer{ 8192 };

  boost::beast::http::request_parser<
    boost::beast::http::vector_body<std::uint8_t>>
    request_parser;
  boost::beast::http::response<boost::beast::http::dynamic_body> response;

  boost::asio::steady_timer socket_kill_deadline;
  boost::asio::steady_timer processing_stop_deadline;

  std::vector<std::shared_ptr<resize_executor>> my_executors;

  std::atomic_bool started_sending_response{ false };

  void write_error_body(std::string_view reason)
  {
    boost::beast::ostream(response.body())
      << "{\"error\": \"" << reason << "\"}";
  }

  void respond_with_data(
    std::variant<processing_result, std::string> const& result)
  {
    auto response_already_sent = started_sending_response.exchange(true);
    if (response_already_sent) {
      std::cerr << "Processing finished, but timeout response was already sent"
                << std::endl;
      return;
    }

    if (std::holds_alternative<std::string>(result)) {
      response.result(boost::beast::http::status::internal_server_error);
      write_error_body(json_sanitize_string(std::get<std::string>(result)));
    } else {
      std::get<processing_result>(result).write_json(
        boost::beast::ostream(response.body()));
    }

    write_response();
  }

  void process_request(boost::beast::error_code read_ec,
                       std::size_t _bytes_transferred)
  {
    auto const& request = request_parser.get();
    response.version(request.version());
    response.keep_alive(false);
    response.set(boost::beast::http::field::content_type, "application/json");

    if (read_ec) {
      if (read_ec == boost::beast::http::error::body_limit) {
        response.result(boost::beast::http::status::payload_too_large);
        write_error_body("error.payload_too_large");
      } else {
        std::cerr << "Error reading request: " << read_ec.message()
                  << std::endl;
        response.result(boost::beast::http::status::internal_server_error);
        write_error_body("error.server_error");
      }

      write_response();
      return;
    }

    auto url = ada::parse<ada::url>("http://example.com" +
                                    std::string(request.target()));
    if (!url) {
      std::cerr << "Error parsing URL: " << request.target() << std::endl;
      response.result(boost::beast::http::status::internal_server_error);
      write_error_body("error.server_error");
      write_response();
      return;
    }
    if (url->get_pathname() != "/api/upload") {
      response.result(boost::beast::http::status::not_found);
      write_error_body("error.not_found");
      write_response();
      return;
    }
    if (request.method() != boost::beast::http::verb::post) {
      response.result(boost::beast::http::status::method_not_allowed);
      write_error_body("error.method_not_allowed");
      write_response();
      return;
    }

    ada::url_search_params params(url->get_search());
    if (!params.has("filename")) {
      response.result(boost::beast::http::status::bad_request);
      write_error_body("error.missing_filename");
      write_response();
      return;
    }

    // add the hook to send processing_timeout error if the processing takes too
    // long then we need to ensure that we write the response data only once.
    // From this point, there are two places where that can happen: in the
    // stop_processing_on_deadline hook, or in respond_with_data, called by the
    // executor. Both of these places (and any added in future) need to check
    // that the response wasn't set yet by the other fibre.
    stop_processing_on_deadline();

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
    processing_stop_deadline.cancel();
    auto self = shared_from_this();

    response.content_length(response.body().size());

    boost::beast::http::async_write(
      socket, response, [self](boost::beast::error_code ec, std::size_t) {
        self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        self->socket_kill_deadline.cancel();
      });
  }

  void kill_socket_on_deadline()
  {
    auto self = shared_from_this();
    socket_kill_deadline.expires_from_now(
      std::chrono::seconds(cfg.socket_kill_timeout_secs));

    socket_kill_deadline.async_wait([self](boost::beast::error_code ec) {
      if (ec) {
        if (ec.value() != boost::asio::error::operation_aborted)
          std::cerr << "Error waiting on socket deadline: " << ec.message()
                    << std::endl;
        return;
      }
      std::cerr << "Timeout: killing socket" << std::endl;
      self->socket.close(ec);
    });
  }

  void stop_processing_on_deadline()
  {
    auto self = shared_from_this();
    processing_stop_deadline.expires_from_now(
      std::chrono::seconds(cfg.processing_timeout_secs));

    processing_stop_deadline.async_wait([self](boost::beast::error_code ec) {
      if (ec) {
        if (ec.value() != boost::asio::error::operation_aborted)
          std::cerr << "Error waiting on processing deadline: " << ec.message()
                    << std::endl;
        return;
      }

      std::cerr << "Timeout: stopping processing" << std::endl;

      for (auto& executor : self->my_executors) {
        executor->cancel();
      }

      auto response_already_sent =
        self->started_sending_response.exchange(true);
      if (response_already_sent)
        return;

      self->response.result(boost::beast::http::status::service_unavailable);
      self->write_error_body("error.processing_timed_out");
      self->write_response();
    });
  }

public:
  http_connection(boost::asio::ip::tcp::socket socket,
                  worker_pool_t& pool,
                  config const& cfg)
    : socket(std::move(socket))
    , pool(pool)
    , cfg(cfg)
    , socket_kill_deadline(socket.get_executor())
    , processing_stop_deadline(socket.get_executor())
  {
  }

  // Initiate the asynchronous operations associated with the connection.
  void start()
  {
    request_parser.body_limit(cfg.upload_limit_bytes);

    auto self = shared_from_this();

    boost::beast::http::async_read(
      socket,
      buffer,
      request_parser,
      [self](boost::beast::error_code ec, std::size_t bytes_transferred) {
        self->process_request(ec, bytes_transferred);
      });

    kill_socket_on_deadline();
  }
};

void
resize_executor::perform_work()
{
  auto result = process_data(std::move(data), cancelled);
  if (std::shared_ptr<http_connection> upgraded = conn.lock()) {
    upgraded->respond_with_data(result);
  } else {
    std::cerr << "Connection was already destroyed" << std::endl;
  }
}

#endif // MAIN_HPP