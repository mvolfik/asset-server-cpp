#ifndef MAIN_HPP
#define MAIN_HPP

#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "../vendor/ada.hpp"

#include "config.hpp"
// #include "image_processing.hpp"
// #include "thread_pool.hpp"
#include "utils.hpp"


// class resize_executor
// {
// private:
//   std::weak_ptr<http_connection> conn;
//   worker_task_data data;
//   config const& cfg;
//   std::atomic_bool cancelled{ false };

// public:
//   resize_executor(std::weak_ptr<http_connection> conn,
//                   worker_task_data&& data,
//                   config const& cfg)
//     : conn(conn)
//     , data(std::move(data))
//     , cfg(cfg)
//   {
//   }

//   void perform_work();
//   void cancel() { cancelled = true; }
// };

// void
// resize_executor::perform_work()
// {
//   if (cancelled) {
//     std::cerr << "Processing cancelled at task start" << std::endl;
//     return;
//   }

//   worker_result_variant worker_result;
//   if (std::holds_alternative<load_image_task_data>(data)) {
//     worker_result =
//       load_image(std::get<load_image_task_data>(std::move(data)), cfg);
//   } else if (std::holds_alternative<resize_to_spec_task_data>(data)) {
//     worker_result = resize_to_spec(std::get<resize_to_spec_task_data>(data));
//   } else {
//     std::cerr << "Internal error (bug): unknown worker_task_data variant"
//               << std::endl;
//     worker_result =
//       error_result{ "error.internal",
//                     boost::beast::http::status::internal_server_error };
//   }

//   if (cancelled) {
//     std::cerr << "Processing cancelled after task finish" << std::endl;
//     return;
//   }

//   std::shared_ptr<http_connection> upgraded = conn.lock();

//   if (upgraded) {
//     if (std::holds_alternative<error_result>(worker_result)) {
//       // handle error
//       upgraded->check_and_respond_with_error(
//         std::get<error_result>(worker_result));
//     } else {
//       // save the result if any, start response if this is the last worker
//       // finishing
//       if (std::holds_alternative<load_image_result>(worker_result)) {
//         auto result = std::get<load_image_result>(std::move(worker_result));
//         upgraded->result = std::move(result.metadata);
//         for (auto const& spec : upgraded->result->dimensions) {
//           resize_to_spec_task_data data{
//             result.image,
//             spec,
//             *upgraded->result,
//           };
//           upgraded->start_task(std::make_shared<resize_executor>(
//             upgraded, worker_task_data{ std::move(data) }, cfg));
//         }
//       } // else: result is empty_result, noop

//       auto value_before_write = upgraded->pending_jobs.fetch_sub(1);
//       if (value_before_write == 1)
//         upgraded->respond();
//     }
//   } else {
//     std::cerr << "Connection was already destroyed" << std::endl;
//   }
// }

#endif // MAIN_HPP