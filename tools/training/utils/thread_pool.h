// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace openzl::training {
class ThreadPool {
public:
  explicit ThreadPool(size_t numThreads_);

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  ~ThreadPool();

  /**
   * This function accepts a callable object and its arguments, packages them
   * into a task, and enqueues the task for execution by the thread pool. It
   * returns a `std::future` object that can be used to retrieve the result of
   * the task once it has been executed. This function is intended to be used
   * for running asynchronous tasks in parallel.
   *
   * @param func The callable object to be executed.
   * @param args The arguments to be passed to the callable object.
   * @return std::future<ReturnType> A future object that holds the result of
   * the task.
   */
  template <class F, class... Args>
  auto run(F &&f, Args &&...args) -> std::future<
      std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using ReturnType =
        std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
    using Task = std::packaged_task<ReturnType()>;

    // Capture callable + args by MOVE into a tuple (no copies of move-only
    // types).
    auto bound = [fn = std::forward<F>(f),
                  tup = std::make_tuple(std::forward<Args>(args)...)]() mutable
        -> ReturnType { return std::apply(fn, tup); };

    auto task = std::make_shared<Task>(std::move(bound));
    auto fut = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queueMutex_);       // correct name
      taskQueue_.emplace([task]() mutable { (*task)(); }); // correct name
    }
    condition_.notify_one(); // correct name
    return fut;
  }

  const size_t numThreads;

private:
  std::mutex queueMutex_;
  std::queue<std::function<void()>> taskQueue_;
  std::vector<std::thread> threads_;
  std::condition_variable condition_;
  bool stop_ = false;
};
} // namespace openzl::training
