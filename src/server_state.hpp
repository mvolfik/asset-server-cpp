#ifndef SERVER_STATE_HPP
#define SERVER_STATE_HPP

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "config.hpp"
#include "thread_pool.hpp"

/**
 * Lightweight structure to pass around references to "global" variables used in
 * many places of the server.
 */
struct server_state
{
  config const& server_config;
  thread_pool& pool;

  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>&
    currently_processing;
  std::mutex& currently_processing_mutex;
};

#endif // SERVER_STATE_HPP