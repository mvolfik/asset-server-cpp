#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

#include <string>

#include <ada.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <openssl/crypto.h>

#include "image_processing.hpp"
#include "server_state.hpp"

struct error_result
{
  std::string error;
  boost::beast::http::status response_code;
};

using work_queue = int;

class http_connection : public std::enable_shared_from_this<http_connection>
{
private:
  boost::asio::ip::tcp::socket socket;
  /** A buffer for performing reads. */
  boost::beast::flat_buffer buffer{ 8192 };

  server_state state;

  boost::beast::http::request_parser<
    boost::beast::http::vector_body<std::uint8_t>>
    request_parser;
  boost::beast::http::response<boost::beast::http::dynamic_body> response;

  boost::asio::steady_timer socket_kill_deadline;
  boost::asio::steady_timer processing_stop_deadline;

  std::atomic<bool> responded{ false };

  std::shared_ptr<image_processor> processor;

  /**
   * Returns true if response should be sent, false otherwise (in the case
   * response was already sent)
   */
  bool start_response()
  {
    socket_kill_deadline.cancel();
    processing_stop_deadline.cancel();

    return !responded.exchange(true);
  }

  void respond_with_error(error_result const& error)
  {
    if (!start_response())
      return;

    response.result(error.response_code);
    response.set(boost::beast::http::field::content_type, "application/json");
    boost::beast::ostream(response.body())
      << "{\"error\": \"" << error.error << "\"}";

    response.content_length(response.body().size());

    boost::beast::http::async_write(
      socket,
      response,
      [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
        self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        self->socket.close();
      });
  }

  void respond_ok()
  {
    if (!start_response())
      return;

    response.result(boost::beast::http::status::ok);
    response.set(boost::beast::http::field::content_type, "application/json");
    {
      auto stream = boost::beast::ostream(response.body());
      processor->write_result_json(stream);
    }

    response.content_length(response.body().size());

    boost::beast::http::async_write(
      socket,
      response,
      [self = shared_from_this()](boost::beast::error_code ec, std::size_t) {
        self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        self->socket.close();
      });
  }

  template<typename T>
  bool is_authorized(boost::beast::http::request<T> const& request)
  {
    if (state.server_config.auth_header_val.empty())
      return true;

    auto token = request[boost::beast::http::field::authorization];
    if (token.size() != state.server_config.auth_header_val.size())
      return false;

    // constant-time comparison to avoid timing attacks
    int ret = CRYPTO_memcmp(token.data(),
                            state.server_config.auth_header_val.data(),
                            state.server_config.auth_header_val.size());
    return ret == 0;
  }

  void process_request(boost::beast::error_code read_ec)
  {
    auto const& request = request_parser.get();
    response.version(request.version());
    response.keep_alive(false);
    response.set(boost::beast::http::field::content_type, "application/json");

    if (read_ec) {
      if (read_ec == boost::beast::http::error::body_limit) {
        respond_with_error({ "error.payload_too_large",
                             boost::beast::http::status::payload_too_large });
      } else {
        std::cerr << "Error reading request: " << read_ec.message()
                  << std::endl;
        respond_with_error(
          { "error.bad_request", boost::beast::http::status::bad_request });
      }

      return;
    }

    auto base_url = ada::url(); // we only receive a partial URL like
                                // /api/upload. We need some fake base URL, to
                                // make ADA parse our incoming part correcly
    auto target_path =  // this is a boost::string_view, we need to convert it
      request.target(); // to std::string_view. ugh.
    auto url = ada::parse<ada::url>(
      std::string_view(target_path.data(), target_path.size()), &base_url);

    if (!url) {
      std::cerr << "Error parsing URL: " << request.target() << std::endl;
      respond_with_error({ "error.internal",
                           boost::beast::http::status::internal_server_error });
      return;
    }
    if (url->get_pathname() != "/api/upload") {
      respond_with_error(
        { "error.not_found", boost::beast::http::status::not_found });
      return;
    }
    if (request.method() != boost::beast::http::verb::post) {
      respond_with_error({ "error.method_not_allowed",
                           boost::beast::http::status::method_not_allowed });
      return;
    }

    ada::url_search_params params(url->get_search());
    if (!params.has("filename")) {
      respond_with_error(
        { "error.missing_filename", boost::beast::http::status::bad_request });
      return;
    }

    if (!is_authorized(request)) {
      respond_with_error(
        { "error.unauthorized", boost::beast::http::status::unauthorized });
      return;
    }

    stop_processing_on_deadline();

    std::cerr << "Starting processing of image of size "
              << request.body().size() << " bytes" << std::endl;

    processor = image_processor::create(
      state,
      [self = weak_from_this()](std::exception const* e) {
        auto shared = self.lock();
        if (!e) {
          if (!shared) {
            std::cerr << "Processing finished, but connection is dead"
                      << std::endl;
            return;
          }

          shared->respond_ok();
          return;
        }

        auto loading_error = dynamic_cast<image_loading_error const*>(e);
        if (loading_error && shared) {
          shared->respond_with_error(
            { "error.invalid_image", boost::beast::http::status::bad_request });
          return;
        }

        std::cerr << "Error processing image: " << e->what() << std::endl;
        if (shared) {
          shared->respond_with_error(
            { "error.internal",
              boost::beast::http::status::internal_server_error });
        }
        return;
      },
      request.body(),
      std::string(*params.get("filename")));
  }

  void kill_socket_on_deadline()
  {
    socket_kill_deadline.expires_from_now(
      std::chrono::seconds(state.server_config.socket_kill_timeout_secs));

    socket_kill_deadline.async_wait(
      [self = shared_from_this()](boost::beast::error_code ec) {
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
      std::chrono::seconds(state.server_config.processing_timeout_secs));

    processing_stop_deadline.async_wait([self](boost::beast::error_code ec) {
      if (ec) {
        if (ec.value() != boost::asio::error::operation_aborted)
          std::cerr << "Error waiting on processing deadline: " << ec.message()
                    << std::endl;
        return;
      }

      std::cerr << "Timeout: stopping processing" << std::endl;

      // if we already responded before, it gets noticed inside
      // respond_with_error
      self->respond_with_error(
        { "error.processing_timed_out",
          boost::beast::http::status::service_unavailable });
    });
  }

public:
  http_connection(boost::asio::ip::tcp::socket socket, server_state state)
    : socket(std::move(socket))
    , state(state)
    , socket_kill_deadline(socket.get_executor())
    , processing_stop_deadline(socket.get_executor())
  {
  }

  void start()
  {
    request_parser.body_limit(state.server_config.upload_limit_bytes);

    boost::beast::http::async_read(
      socket,
      buffer,
      request_parser,
      [self = shared_from_this()](boost::beast::error_code ec,
                                  std::size_t _bytes_transferred) {
        self->process_request(ec);
      });

    kill_socket_on_deadline();
  }
};

#endif // HTTP_CONNECTION_HPP
