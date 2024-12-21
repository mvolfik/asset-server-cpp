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
  using Executor = std::function<void()>;
  std::mutex mutex;
  std::condition_variable cv;
  /// lock the mutex before accessing this, wait on CV if no tasks are available
  std::deque<Executor> tasks;
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
          Executor executor = std::move(tasks.front());
          tasks.pop_front();
          lock.unlock();
          if (rand() % 2 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          executor();
        }
      });
    }
  }

  void add_task(Executor&& task)
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
  enum class State : std::uint8_t
  {
    Running = 0,
    Done_OK = 1,
    Done_Error = 2,
  };

  thread_pool& pool;

  /**
   * Starts as Running, then at least one task starts, then any tasks may start
   * running or finish. Finally, either the number of pending tasks reaches 0
   * and state is set to Done_OK, or any task throws an exception and the state
   * is set to Done_Error. After the first Done_* state is set, the state
   * mustn't be changed again and no more tasks will be added or started.
   *
   * Specifically, if all tasks ever finish, this group can't be used for
   * starting any further tasks.
   *
   * After any task raises an exception, no more tasks will be added or started,
   * but it is not a logic error to do so - only warnings will be printed, and
   * nothing will happen. Even in this case, the number of pending tasks should
   * be kept accurate and eventually reach 0, but it serves no real purpose.
   */
  std::atomic<State> state{ State::Running };
  std::atomic<std::int16_t> pending_tasks{ 0 };
  std::function<void(std::exception const&)> on_error;
  std::function<void()> on_finish;

  bool set_state_if_running(State new_state)
  {
    State old = State::Running;
    return state.compare_exchange_strong(old, new_state);
  }

public:
  task_group(thread_pool& pool,
             std::function<void(std::exception const&)>&& on_error,
             std::function<void()>&& on_finish)
    : pool(pool)
    , on_error(std::move(on_error))
    , on_finish(std::move(on_finish))
  {
  }

  ~task_group()
  {
    std::int16_t pending = pending_tasks.load();
    if (pending > 0) {
      std::cerr << "Warning: destroying task group with  " << pending
                << " pending tasks" << std::endl;
    }
  }

  class cancelled_error : public std::runtime_error
  {
  public:
    cancelled_error()
      : std::runtime_error("Task group was cancelled")
    {
    }
  };

  void cancel()
  {
    bool exchanged = set_state_if_running(State::Done_Error);
    if (exchanged) {
      on_error(cancelled_error());
    }
  }

  template<typename Fn>
  void add_task(Fn&& task)
  {
    {
      auto s = state.load();
      if (s == State::Done_OK)
        throw std::logic_error("Cannot add task to a finished group");

      if (s == State::Done_Error) {
        std::cerr << "Warning: adding task to a group that already errored"
                  << std::endl;
        return;
      }
    }

    pending_tasks.fetch_add(1);

    pool.add_task([this, task = std::move(task)]() {
      {
        auto s = state.load();
        if (s == State::Done_OK) {
          pending_tasks.fetch_sub(1);
          throw std::logic_error(
            "Task is about to start running in a finished group");
        }

        if (s == State::Done_Error) {
          std::cerr << "Warning: not starting task, since this group already "
                       "errored"
                    << std::endl;
          pending_tasks.fetch_sub(1);
          return;
        }
      }

      try {
        task();
      } catch (std::exception const& e) {
        bool exchanged = set_state_if_running(State::Done_Error);
        pending_tasks.fetch_sub(1);
        if (exchanged) {
          // we're the first error
          on_error(e);

          // it is race-free to read the state here - in the compare-exchange
          // above, it was not Running, so either of the Done_* states, so it
          // won't be changed again since then
        } else if (state.load() == State::Done_OK) {
          // this is our bug and should never happen
          throw std::logic_error("A task finished (and errored) after group "
                                 "was marked as done. The exception: " +
                                 std::string(e.what()));
        } else {
          // this is a normal scenario
          std::cerr << "Error in task (not the first error in group, there's "
                       "noone to report to): "
                    << e.what() << std::endl;
        }
        return;
      }

      auto old_value = pending_tasks.fetch_sub(1);
      if (old_value == 1) {
        if (state.load() == State::Done_OK) {
          // this isn't exactly in sync, but the invariant holds nonetheless
          throw std::logic_error(
            "The number of pending tasks reached 0 with this task, but the "
            "group is already marked as done");
        }
        bool exchanged = set_state_if_running(State::Done_OK);
        if (exchanged)
          on_finish();
        // else: the group already errored
      } else if (old_value < 1) {
        // this is our bug and should never happen
        throw std::logic_error("The number of pending tasks is negative: " +
                               std::to_string(old_value - 1));
      }
    });
  }
};

#endif // THREAD_POOL_HPP