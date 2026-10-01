#ifndef COASIO_DETAIL_BLOCKING_POOL_HPP
#define COASIO_DETAIL_BLOCKING_POOL_HPP

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>

namespace coasio::detail::blocking {
struct control_block_base {
  virtual void execute() = 0;
  virtual ~control_block_base() = default;
};

class pool {
public:
  // TODO: migrate to a dynamic resizing pool
  explicit pool(const std::size_t n_threads) {
    threads_.reserve(n_threads);
    for (std::size_t i = 0; i < n_threads; ++i)
      threads_.emplace_back([this] { worker_loop(); });
  }

  pool(const pool &) = delete;
  pool &operator=(const pool &) = delete;

  ~pool() {
    {
      std::lock_guard lock(mtx_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto &t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    threads_.clear();
  }

  bool enqueue(std::shared_ptr<control_block_base> item) {
    std::lock_guard lock(mtx_);
    if (stop_)
      return false;
    queue_.push_back(std::move(item));
    cv_.notify_one();
    return true;
  }

  bool try_cancel(const std::shared_ptr<control_block_base> &item) {
    std::lock_guard lock(mtx_);
    const auto it = std::ranges::find(queue_, item);
    if (it == queue_.end())
      return false;
    queue_.erase(it);
    return true;
  }

private:
  void worker_loop() {
    for (;;) {
      std::shared_ptr<control_block_base> item;
      {
        std::unique_lock lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty() || stop_; });
        if (queue_.empty()) {
          if (stop_)
            return;
          continue;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
      }
      item->execute();
    }
  }

  std::mutex mtx_;
  std::condition_variable cv_;
  std::deque<std::shared_ptr<control_block_base>> queue_;
  bool stop_ = false;
  std::vector<std::thread> threads_;
};
} // namespace coasio::detail::blocking

#endif // !COASIO_DETAIL_BLOCKING_POOL_HPP