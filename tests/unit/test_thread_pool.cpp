/**
 * @file tests/unit/test_thread_pool.cpp
 * @brief Regression tests for ThreadPool's OS scheduling priority option.
 *
 * task_pool is started at critical OS priority so that client input dispatch
 * (which shares this pool with other housekeeping tasks) isn't starved relative
 * to Sunshine's own encode/audio threads under CPU pressure. These tests guard
 * the mechanics of that: the new start() overload must not disturb normal task
 * execution (ordering, delayed tasks, cancellation), on top of or without a
 * priority request.
 */
#include "../tests_common.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <src/thread_pool.h>

using namespace std::chrono_literals;

TEST(ThreadPool, LegacyStartStillRunsTasks) {
  thread_pool_util::ThreadPool pool;
  pool.start(1);

  std::atomic_bool ran {false};
  pool.push([&]() {
    ran.store(true, std::memory_order_release);
  });

  auto deadline = std::chrono::steady_clock::now() + 1s;
  while (!ran.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  pool.stop();
  pool.join();

  EXPECT_TRUE(ran.load());
}

TEST(ThreadPool, StartWithPriorityStillRunsTasks) {
  thread_pool_util::ThreadPool pool;
  pool.start(1, platf::thread_priority_e::critical, "TestPool::worker");

  std::atomic_bool ran {false};
  pool.push([&]() {
    ran.store(true, std::memory_order_release);
  });

  auto deadline = std::chrono::steady_clock::now() + 1s;
  while (!ran.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  pool.stop();
  pool.join();

  EXPECT_TRUE(ran.load());
}

TEST(ThreadPool, StartWithPriorityPreservesDelayedTaskOrdering) {
  thread_pool_util::ThreadPool pool;
  pool.start(1, platf::thread_priority_e::critical, "TestPool::worker");

  std::atomic_int counter {0};
  std::atomic_int firstResult {-1};
  std::atomic_int secondResult {-1};

  // Second delayed task is scheduled to fire after the first; both must still
  // run, in the expected order, with priority elevation enabled.
  pool.pushDelayed([&]() {
    firstResult.store(counter.fetch_add(1), std::memory_order_release);
  },
                   10ms);
  pool.pushDelayed([&]() {
    secondResult.store(counter.fetch_add(1), std::memory_order_release);
  },
                   20ms);

  auto deadline = std::chrono::steady_clock::now() + 1s;
  while (secondResult.load(std::memory_order_acquire) < 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  pool.stop();
  pool.join();

  EXPECT_EQ(firstResult.load(), 0);
  EXPECT_EQ(secondResult.load(), 1);
}

TEST(ThreadPool, StartWithPriorityAllowsCancellation) {
  thread_pool_util::ThreadPool pool;
  pool.start(1, platf::thread_priority_e::critical, "TestPool::worker");

  std::atomic_bool ran {false};
  auto id = pool.pushDelayed([&]() {
    ran.store(true, std::memory_order_release);
  },
                             50ms)
              .task_id;

  pool.cancel(id);

  std::this_thread::sleep_for(100ms);

  pool.stop();
  pool.join();

  EXPECT_FALSE(ran.load());
}
