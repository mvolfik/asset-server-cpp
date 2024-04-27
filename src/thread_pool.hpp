#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

/// Creates a pool of N threads, that then in parallel execute submitted tasks
///
/// Executor is an object that has .perform_work
template<typename Executor>
class thread_pool
{
private:
  std::mutex mutex;
  std::condition_variable cv;
  /// lock the mutex before accessing this, wait on CV if no tasks are available
  std::deque<std::shared_ptr<Executor>> tasks;
  std::vector<std::thread>
    threads; // general TODO (not just here): do some proper shutdown on SIGINT
  boost::asio::io_context ctx;

public:
  thread_pool(int n)
  {
    for (int i = 0; i < n; i++) {
      threads.emplace_back([this] {
        while (true) {
          std::unique_lock<std::mutex> lock(mutex);
          cv.wait(lock, [this] { return !tasks.empty(); });
          std::shared_ptr<Executor> executor = tasks.front();
          tasks.pop_front();
          lock.unlock();
          executor->perform_work();
        }
      });
    }
  }

  void add_task(std::shared_ptr<Executor> conn)
  {
    std::unique_lock<std::mutex> lock(mutex);
    tasks.emplace_back(conn);
    cv.notify_one();
  }
};

#endif // THREAD_POOL_HPP