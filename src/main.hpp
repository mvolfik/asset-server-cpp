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
  worker_task_data data;
  config const& cfg;
  std::atomic_bool cancelled{ false };

public:
  resize_executor(std::weak_ptr<http_connection> conn,
                  worker_task_data&& data,
                  config const& cfg)
    : conn(conn)
    , data(std::move(data))
    , cfg(cfg)
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

  boost::asio::ip::tcp::socket socket;
  /** A buffer for performing reads. */
  boost::beast::flat_buffer buffer{ 8192 };

  worker_pool_t& pool;
  config const& cfg;

  boost::beast::http::request_parser<
    boost::beast::http::vector_body<std::uint8_t>>
    request_parser;
  boost::beast::http::response<boost::beast::http::dynamic_body> response;

  boost::asio::steady_timer socket_kill_deadline;
  boost::asio::steady_timer processing_stop_deadline;

  using pending_jobs_t = std::uint16_t;

  /**
   * This is incremented for the first time when the first worker is started. If
   * a worker needs to start more jobs, it should increment this value before
   * decrementing it (because it itself finished). That way, if a worker
   * finishes and notices the new value is 0, it knows it was the last one and
   * can start the response.
   */
  std::atomic<pending_jobs_t> pending_jobs{ 0 };

  /**
   * When an error (or timeout) happens, do
   * pending_jobs.fetch_and_set(SET_VALUE). If the old value was
   * < COMPARE_MIN_VALUE, send an error response.
   *
   * Why this logic: after some worker thread (or timeout) sets pending_jobs
   * to SET_VALUE, it might be decremented by some workers finishing, so you
   * can't check by direct comparison. However, we should never spawn more
   * workers than pending_jobs_t::max/2, so you can still detect that an error
   * already happened (and response was therefore sent) if pending_jobs is >=
   * COMPARE_MIN_VALUE.
   */
  static constexpr pending_jobs_t PENDING_JOBS_ERROR_COMPARE_MIN_VALUE =
    std::numeric_limits<pending_jobs_t>::max() / 2;
  static constexpr pending_jobs_t PENDING_JOBS_ERROR_SET_VALUE =
    std::numeric_limits<pending_jobs_t>::max();

  /**
   * May only be written by the first started worker (which reads the image and
   * generates variant sizes + formats), and may only be read by respond(),
   * which is called after the last worker finishes. These stages are
   * synchronized by pending_jobs.
   */
  std::optional<image_metadata> result;

  std::vector<std::shared_ptr<resize_executor>> my_executors;

  void write_error_body(std::string_view reason)
  {
    boost::beast::ostream(response.body())
      << "{\"error\": \"" << reason << "\"}";
  }

  /**
   * This is called from worker::perform_work() when if finishes its job, it
   * is the last one to do so, and error response wasn't sent. All this info is
   * synchronized by the pending_jobs atomic variable.
   */
  void respond()
  {
    if (!result) {
      std::cerr
        << "Internal error (bug): called respond(), but no result was set"
        << std::endl;
      check_and_respond_with_error(error_result{
        "error.internal", boost::beast::http::status::internal_server_error });
      return;
    }

    auto stream = boost::beast::ostream(response.body());
    result->write_json(stream);
    stream.flush();

    write_response();
  }

  void check_and_respond_with_error(error_result const& error)
  {
    auto value_before_write =
      pending_jobs.exchange(PENDING_JOBS_ERROR_SET_VALUE);
    if (value_before_write >= PENDING_JOBS_ERROR_COMPARE_MIN_VALUE)
      return;

    // abort all executors
    for (auto& executor : my_executors) {
      executor->cancel();
    }

    response.result(error.response_code);
    write_error_body(error.error);
    write_response();
  }

  void start_task(std::shared_ptr<resize_executor> executor)
  {
    my_executors.push_back(executor);
    pending_jobs.fetch_add(1);
    pool.add_task(executor);
  }

  void process_request(boost::beast::error_code read_ec)
  {
    auto const& request = request_parser.get();
    response.version(request.version());
    response.keep_alive(false);
    response.set(boost::beast::http::field::content_type, "application/json");

    if (read_ec) {
      if (read_ec == boost::beast::http::error::body_limit) {
        check_and_respond_with_error(
          error_result{ "error.payload_too_large",
                        boost::beast::http::status::payload_too_large });
      } else {
        std::cerr << "Error reading request: " << read_ec.message()
                  << std::endl;
        check_and_respond_with_error(error_result{
          "error.bad_request", boost::beast::http::status::bad_request });
      }

      return;
    }

    auto url = ada::parse<ada::url>("http://example.com" +
                                    std::string(request.target()));
    if (!url) {
      std::cerr << "Error parsing URL: " << request.target() << std::endl;
      check_and_respond_with_error(error_result{
        "error.internal", boost::beast::http::status::internal_server_error });
      return;
    }
    if (url->get_pathname() != "/api/upload") {
      check_and_respond_with_error(error_result{
        "error.not_found", boost::beast::http::status::not_found });
      return;
    }
    if (request.method() != boost::beast::http::verb::post) {
      check_and_respond_with_error(
        error_result{ "error.method_not_allowed",
                      boost::beast::http::status::method_not_allowed });
      return;
    }

    ada::url_search_params params(url->get_search());
    if (!params.has("filename")) {
      check_and_respond_with_error(error_result{
        "error.missing_filename", boost::beast::http::status::bad_request });
      return;
    }

    // Add the hook to send processing_timeout error.
    // From now on, we have multiple places that can send a response, so we need
    // to take care all are synchronized using the mechanism described in
    // the comments above pending_jobs.
    stop_processing_on_deadline();

    load_image_task_data data{ request.body(),
                               std::string(params.get("filename").value()) };
    start_task(std::make_shared<resize_executor>(
      weak_from_this(), worker_task_data{ std::move(data) }, cfg));
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

      self->check_and_respond_with_error(
        error_result{ "error.processing_timed_out",
                      boost::beast::http::status::service_unavailable });
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
      [self](boost::beast::error_code ec, std::size_t _bytes_transferred) {
        self->process_request(ec);
      });

    kill_socket_on_deadline();
  }
};

void
resize_executor::perform_work()
{
  if (cancelled) {
    std::cerr << "Processing cancelled at task start" << std::endl;
    return;
  }

  worker_result_variant worker_result;
  if (std::holds_alternative<load_image_task_data>(data)) {
    worker_result =
      load_image(std::get<load_image_task_data>(std::move(data)), cfg);
  } else if (std::holds_alternative<resize_to_spec_task_data>(data)) {
    worker_result =
      resize_to_spec(std::get<resize_to_spec_task_data>(std::move(data)));
  } else {
    std::cerr << "Internal error (bug): unknown worker_task_data variant"
              << std::endl;
    worker_result =
      error_result{ "error.internal",
                    boost::beast::http::status::internal_server_error };
  }

  if (cancelled) {
    std::cerr << "Processing cancelled after task finish" << std::endl;
    return;
  }

  std::shared_ptr<http_connection> upgraded = conn.lock();

  if (upgraded) {
    if (std::holds_alternative<error_result>(worker_result)) {
      // handle error
      upgraded->check_and_respond_with_error(
        std::get<error_result>(worker_result));
    } else {
      // save the result if any, start response if this is the last worker
      // finishing
      if (std::holds_alternative<load_image_result>(worker_result)) {
        auto result = std::get<load_image_result>(std::move(worker_result));
        upgraded->result = std::move(result.metadata);
        for (auto const& spec : upgraded->result->dimensions) {
          resize_to_spec_task_data data{
            result.image,
            spec,
            *upgraded->result,
          };
          upgraded->start_task(std::make_shared<resize_executor>(
            upgraded, worker_task_data{ std::move(data) }, cfg));
        }
      } // else: result is empty_result, noop

      auto value_before_write = upgraded->pending_jobs.fetch_sub(1);
      if (value_before_write == 1)
        upgraded->respond();
    }
  } else {
    std::cerr << "Connection was already destroyed" << std::endl;
  }
}

#endif // MAIN_HPP