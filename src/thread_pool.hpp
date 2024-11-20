#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

/// Creates a pool of N threads, that then in parallel execute submitted tasks
class thread_pool
{
private:
  std::mutex mutex;
  std::condition_variable cv;
  /// lock the mutex before accessing this, wait on CV if no tasks are available
  std::deque<std::function<void()>> tasks;
  std::vector<std::thread> threads;
  std::atomic_bool shutdown{ false };

public:
  thread_pool(unsigned n)
  {
    for (unsigned i = 0; i < n; i++) {
      threads.emplace_back([this] {
        while (!shutdown) {
          std::unique_lock<std::mutex> lock(mutex);
          cv.wait(lock, [this] { return !tasks.empty() || shutdown; });
          if (shutdown)
            break;
          std::function<void()> executor = std::move(tasks.front());
          tasks.pop_front();
          lock.unlock();
          executor();
        }
      });
    }
  }

  void add_task(std::function<void()>&& task)
  {
    std::unique_lock<std::mutex> lock(mutex);
    tasks.push_back(std::move(task));
    cv.notify_one();
  }

  void blocking_shutdown()
  {
    shutdown = true;
    cv.notify_all();
    for (auto& t : threads)
      t.join();
  }
};

/**
 * A task group handles a collection of related tasks, keeping track of how many
 * are pending. When all tasks are done, it calls a callback. If any task throws
 * an exception, the group is marked as done and the error callback is called.
 *
 * Any handling of results from tasks must be taken care of by the user, as the
 * task return value is ignored.
 */
class task_group
{
private:
  thread_pool& pool;
  std::atomic_bool is_done{ false };
  std::atomic_uint pending_tasks{ 0 };
  std::function<void(std::exception const&)> on_error;
  std::function<void()> on_finish;

public:
  task_group(thread_pool& pool,
             std::function<void(std::exception const&)>&& on_error,
             std::function<void()>&& on_finish)
    : pool(pool)
    , on_error(std::move(on_error))
    , on_finish(std::move(on_finish))
  {
  }

  template<typename Fn>
  void add_task(Fn&& task)
  {
    if (is_done)
      throw std::runtime_error("Cannot add task to a finished group");

    pending_tasks.fetch_add(1);
    pool.add_task([this, task = std::move(task)]() {
      try {
        task();
      } catch (std::exception const& e) {
        if (is_done.exchange(true)) {
          // is done was already set - this is a second or later error
          std::cerr << "Error in task after group was marked as done: "
                    << e.what() << std::endl;
        } else {
          on_error(e);
        }
        return;
      }
      if (pending_tasks.fetch_sub(1) == 1) {
        // we were the last task to finish
        if (!is_done.exchange(true))
          on_finish();
        // else: the group already errored
      }
    });
  }
};

#endif // THREAD_POOL_HPP