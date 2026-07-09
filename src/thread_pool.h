/**
 * @file src/thread_pool.h
 * @brief Declarations for the thread pool system.
 */
#pragma once

// standard includes
#include <optional>
#include <string>
#include <thread>

// local includes
#include "platform/common.h"
#include "task_pool.h"

namespace thread_pool_util {
  /**
   * Allow threads to execute unhindered while keeping full control over the threads.
   */
  class ThreadPool: public task_pool_util::TaskPool {
  public:
    typedef TaskPool::__task __task;

  private:
    std::vector<std::thread> _thread;

    std::condition_variable _cv;
    std::mutex _lock;

    bool _continue;

    // When set, every worker thread adjusts its own OS scheduling priority to this
    // value before servicing tasks.
    std::optional<platf::thread_priority_e> _priority;
    std::string _thread_name {"TaskPool::worker"};

  public:
    ThreadPool():
        _continue {false} {
    }

    explicit ThreadPool(int threads):
        _thread(threads),
        _continue {true} {
      for (auto &t : _thread) {
        t = std::thread(&ThreadPool::_main, this);
      }
    }

    ~ThreadPool() noexcept {
      if (!_continue) {
        return;
      }

      stop();
      join();
    }

    template<class Function, class... Args>
    auto push(Function &&newTask, Args &&...args) {
      std::lock_guard lg(_lock);
      auto future = TaskPool::push(std::forward<Function>(newTask), std::forward<Args>(args)...);

      _cv.notify_one();
      return future;
    }

    void pushDelayed(std::pair<__time_point, __task> &&task) {
      std::lock_guard lg(_lock);

      TaskPool::pushDelayed(std::move(task));
    }

    template<class Function, class X, class Y, class... Args>
    auto pushDelayed(Function &&newTask, std::chrono::duration<X, Y> duration, Args &&...args) {
      std::lock_guard lg(_lock);
      auto future = TaskPool::pushDelayed(std::forward<Function>(newTask), duration, std::forward<Args>(args)...);

      // Update all timers for wait_until
      _cv.notify_all();
      return future;
    }

    void start(int threads) {
      _continue = true;

      _thread.resize(threads);

      for (auto &t : _thread) {
        t = std::thread(&ThreadPool::_main, this);
      }
    }

    /**
     * @brief Start the pool with a dedicated name and OS thread priority for its workers.
     * @param threads Number of worker threads.
     * @param priority OS scheduling priority every worker adjusts itself to on start.
     * @param name Name reported to development tools (e.g. debuggers, `set_thread_name`).
     */
    void start(int threads, platf::thread_priority_e priority, std::string name) {
      _priority = priority;
      _thread_name = std::move(name);
      start(threads);
    }

    void stop() {
      std::lock_guard lg(_lock);

      _continue = false;
      _cv.notify_all();
    }

    void join() {
      for (auto &t : _thread) {
        t.join();
      }
    }

  public:
    void _main() {
      if (_priority) {
        platf::adjust_thread_priority(*_priority);
      }

      while (_continue) {
        if (auto task = this->pop()) {
          (*task)->run();
        } else {
          std::unique_lock uniq_lock(_lock);

          if (ready()) {
            continue;
          }

          if (!_continue) {
            break;
          }

          if (auto tp = next()) {
            _cv.wait_until(uniq_lock, *tp);
          } else {
            _cv.wait(uniq_lock);
          }
        }
      }

      // Execute remaining tasks
      while (auto task = this->pop()) {
        (*task)->run();
      }
    }
  };
}  // namespace thread_pool_util
