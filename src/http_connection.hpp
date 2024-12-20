#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

#include <string>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

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
  //   boost::asio::ip::tcp::socket socket;
  //   /** A buffer for performing reads. */
  //   boost::beast::flat_buffer buffer{ 8192 };

  //   work_queue& pool;
  //   config const& cfg;

  //   boost::beast::http::request_parser<
  //     boost::beast::http::vector_body<std::uint8_t>>
  //     request_parser;
  //   boost::beast::http::response<boost::beast::http::dynamic_body> response;

  //   boost::asio::steady_timer socket_kill_deadline;
  //   boost::asio::steady_timer processing_stop_deadline;

  //   using pending_jobs_t = std::uint16_t;

  //   void write_error_body(std::string_view reason)
  //   {
  //     boost::beast::ostream(response.body())
  //       << "{\"error\": \"" << reason << "\"}";
  //   }

  //   /**
  //    * This is called from worker::perform_work() when if finishes its job,
  //    it
  //    * is the last one to do so, and error response wasn't sent. All this
  //    info is
  //    * synchronized by the pending_jobs atomic variable.
  //    */
  //   void respond()
  //   {
  //     if (!result) {
  //       std::cerr
  //         << "Internal error (bug): called respond(), but no result was set"
  //         << std::endl;
  //       check_and_respond_with_error(error_result{
  //         "error.internal", boost::beast::http::status::internal_server_error
  //         });
  //       return;
  //     }

  //     auto stream = boost::beast::ostream(response.body());
  //     result->write_json(stream);
  //     stream.flush();

  //     write_response();
  //   }

  //   void check_and_respond_with_error(error_result const& error)
  //   {
  //     auto value_before_write =
  //       pending_jobs.exchange(PENDING_JOBS_ERROR_SET_VALUE);
  //     if (value_before_write >= PENDING_JOBS_ERROR_COMPARE_MIN_VALUE)
  //       return;

  //     // abort all executors
  //     for (auto& executor : my_executors) {
  //       executor->cancel();
  //     }

  //     response.result(error.response_code);
  //     write_error_body(error.error);
  //     write_response();
  //   }

  //   void start_task(std::shared_ptr<resize_executor> executor)
  //   {
  //     my_executors.push_back(executor);
  //     pending_jobs.fetch_add(1);
  //     pool.add_task(executor);
  //   }

  //   template<typename T>
  //   bool is_authorized(boost::beast::http::request<T> const& request)
  //   {
  //     if (cfg.auth_header_val.empty())
  //       return true;
  //     auto token = request[boost::beast::http::field::authorization];
  //     return (token == cfg.auth_header_val);
  //   }

  //   void process_request(boost::beast::error_code read_ec)
  //   {
  //     auto const& request = request_parser.get();
  //     response.version(request.version());
  //     response.keep_alive(false);
  //     response.set(boost::beast::http::field::content_type,
  //     "application/json");

  //     if (read_ec) {
  //       if (read_ec == boost::beast::http::error::body_limit) {
  //         check_and_respond_with_error(
  //           error_result{ "error.payload_too_large",
  //                         boost::beast::http::status::payload_too_large });
  //       } else {
  //         std::cerr << "Error reading request: " << read_ec.message()
  //                   << std::endl;
  //         check_and_respond_with_error(error_result{
  //           "error.bad_request", boost::beast::http::status::bad_request });
  //       }

  //       return;
  //     }

  //     auto url = ada::parse<ada::url>("http://example.com" +
  //                                     std::string(request.target()));
  //     if (!url) {
  //       std::cerr << "Error parsing URL: " << request.target() << std::endl;
  //       check_and_respond_with_error(error_result{
  //         "error.internal", boost::beast::http::status::internal_server_error
  //         });
  //       return;
  //     }
  //     if (url->get_pathname() != "/api/upload") {
  //       check_and_respond_with_error(error_result{
  //         "error.not_found", boost::beast::http::status::not_found });
  //       return;
  //     }
  //     if (request.method() != boost::beast::http::verb::post) {
  //       check_and_respond_with_error(
  //         error_result{ "error.method_not_allowed",
  //                       boost::beast::http::status::method_not_allowed });
  //       return;
  //     }

  //     ada::url_search_params params(url->get_search());
  //     if (!params.has("filename")) {
  //       check_and_respond_with_error(error_result{
  //         "error.missing_filename", boost::beast::http::status::bad_request
  //         });
  //       return;
  //     }

  //     if (!is_authorized(request)) {
  //       check_and_respond_with_error(error_result{
  //         "error.unauthorized", boost::beast::http::status::unauthorized });
  //       return;
  //     }

  //     // Add the hook to send processing_timeout error.
  //     // From now on, we have multiple places that can send a response, so we
  //     need
  //     // to take care all are synchronized using the mechanism described in
  //     // the comments above pending_jobs.
  //     stop_processing_on_deadline();

  //     load_image_task_data data{ request.body(),
  //                                std::string(params.get("filename").value())
  //                                };
  //     start_task(std::make_shared<resize_executor>(
  //       weak_from_this(), worker_task_data{ std::move(data) }, cfg));
  //   }

  //   void write_response()
  //   {
  //     processing_stop_deadline.cancel();
  //     auto self = shared_from_this();

  //     response.content_length(response.body().size());

  //     boost::beast::http::async_write(
  //       socket, response, [self](boost::beast::error_code ec, std::size_t) {
  //         self->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send,
  //         ec); self->socket_kill_deadline.cancel();
  //       });
  //   }

  //   void kill_socket_on_deadline()
  //   {
  //     auto self = shared_from_this();
  //     socket_kill_deadline.expires_from_now(
  //       std::chrono::seconds(cfg.socket_kill_timeout_secs));

  //     socket_kill_deadline.async_wait([self](boost::beast::error_code ec) {
  //       if (ec) {
  //         if (ec.value() != boost::asio::error::operation_aborted)
  //           std::cerr << "Error waiting on socket deadline: " << ec.message()
  //                     << std::endl;
  //         return;
  //       }
  //       std::cerr << "Timeout: killing socket" << std::endl;
  //       self->socket.close(ec);
  //     });
  //   }

  //   void stop_processing_on_deadline()
  //   {
  //     auto self = shared_from_this();
  //     processing_stop_deadline.expires_from_now(
  //       std::chrono::seconds(cfg.processing_timeout_secs));

  //     processing_stop_deadline.async_wait([self](boost::beast::error_code ec)
  //     {
  //       if (ec) {
  //         if (ec.value() != boost::asio::error::operation_aborted)
  //           std::cerr << "Error waiting on processing deadline: " <<
  //           ec.message()
  //                     << std::endl;
  //         return;
  //       }

  //       std::cerr << "Timeout: stopping processing" << std::endl;

  //       self->check_and_respond_with_error(
  //         error_result{ "error.processing_timed_out",
  //                       boost::beast::http::status::service_unavailable });
  //     });
  //   }

  // public:
  //   http_connection(boost::asio::ip::tcp::socket socket,
  //                   work_queue& pool,
  //                   config const& cfg)
  //     : socket(std::move(socket))
  //     , pool(pool)
  //     , cfg(cfg)
  //     , socket_kill_deadline(socket.get_executor())
  //     , processing_stop_deadline(socket.get_executor())
  //   {
  //   }

  //   // Initiate the asynchronous operations associated with the connection.
  //   void start()
  //   {
  //     request_parser.body_limit(cfg.upload_limit_bytes);

  //     auto self = shared_from_this();

  //     boost::beast::http::async_read(
  //       socket,
  //       buffer,
  //       request_parser,
  //       [self](boost::beast::error_code ec, std::size_t _bytes_transferred) {
  //         self->process_request(ec);
  //       });

  //     kill_socket_on_deadline();
  //   }
public:
  http_connection(boost::asio::ip::tcp::socket socket, server_state state)
  {
    std::cerr << "connection created\n";
  }

  void start() { std::cerr << "connection started\n"; }
};

#endif // HTTP_CONNECTION_HPP
