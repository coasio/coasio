#ifndef COASIO_RUNTIME_HPP
#define COASIO_RUNTIME_HPP

#include <asio/io_context.hpp>
#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <expected>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "cancel_scope.hpp"
#include "detail/blocking/pool.hpp"
#include "detail/blocking/spawn_blocking.hpp"
#include "detail/fwd.hpp"
#include "detail/root_node.hpp"
#include "sync/oneshot.hpp"
#include "task.hpp"

namespace coasio {
template <typename T> class JoinHandle;

class worker {
  runtime *runtime_;

public:
  explicit worker(runtime *runtime) : runtime_(runtime) {}

  void run() const;
};

class io_worker {
  runtime *runtime_;

public:
  explicit io_worker(runtime *runtime) : runtime_(runtime) {}

  void run() const;
};

class runtime {
  std::queue<std::coroutine_handle<>> global_tasks_;
  std::mutex global_tasks_queue_mutex_;
  std::condition_variable global_tasks_queue_cv_;
  std::atomic<bool> stop_requested_{false};
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::vector<std::thread> io_worker_threads_;
  std::vector<std::thread> worker_threads_;

  std::mutex roots_mutex_;
  detail::root_node *roots_head_ = nullptr;
  std::size_t live_roots_ = 0;
  std::condition_variable roots_drained_cv_;

  std::unique_ptr<detail::blocking::pool> blocking_pool_;
  std::once_flag blocking_pool_once_;
  std::size_t blocking_pool_size_;

  static inline thread_local runtime *current_runtime_ = nullptr;

  template <typename T> JoinHandle<T> _spawn(task<T> task) {
    context_guard guard(this);

    if (!task)
      return JoinHandle<T>{};

    auto scope = std::make_shared<cancel_scope>();
    auto [tx, rx] = sync::make_oneshot<detail::root_outcome<T>>();

    task.set_owning_scope(scope);
    task.handle().promise().result_sender_ = std::move(tx);
    task.handle().promise().owner_rt_ = this;
    task.handle().promise().scope_raw_ = scope.get();

    auto handle = task.detach();
    register_root(
        handle.promise()
            .get_root_node()); // TODO: investigate why: cannot convert argument
                               // 1 from '_CoroPromise' to
                               // 'coasio::detail::root_node *'
    schedule(handle);

    return JoinHandle<T>{this, std::move(scope), std::move(rx)};
  }

  template <typename T> T _block_on(task<T> t) {
    context_guard guard(this);

    using promise_t = std::conditional_t<std::is_void_v<T>, std::promise<void>,
                                         std::promise<T>>;

    auto promise = std::make_shared<promise_t>();
    auto future = promise->get_future();

    spawn([](task<T> t, std::shared_ptr<promise_t> p) -> task<void> {
      try {
        if constexpr (std::is_void_v<T>) {
          co_await std::move(t);
          p->set_value();
        } else {
          T result = co_await std::move(t);
          p->set_value(std::move(result));
        }
      } catch (...) {
        p->set_exception(std::current_exception());
      }
    }(std::move(t), promise));

    return future.get();
  }

public:
  explicit runtime(size_t io_worker_count = 1, size_t worker_count = 0,
                   size_t blocking_pool_size = 1);

  ~runtime();

  static runtime *current() noexcept { return current_runtime_; }

  struct context_guard {
    runtime *prev_;

    explicit context_guard(runtime *rt) noexcept : prev_(current_runtime_) {
      current_runtime_ = rt;
    }

    ~context_guard() noexcept { current_runtime_ = prev_; }
  };

  detail::blocking::pool &blocking_pool() {
    std::call_once(blocking_pool_once_, [this] {
      blocking_pool_ =
          std::make_unique<detail::blocking::pool>(blocking_pool_size_);
    });
    return *blocking_pool_;
  }

  template <typename Arg> auto spawn(Arg &&arg) {
    if constexpr (is_task_v<Arg>) {
      return _spawn(std::forward<Arg>(arg));
    } else if constexpr (std::invocable<Arg> &&
                         is_task_v<std::invoke_result_t<Arg>>) {
      return _spawn(std::invoke(std::forward<Arg>(arg)));
    } else {
      static_assert(
          sizeof(Arg) == 0, // false
          "runtime::spawn(F) requires F to be a coasio::task<T>, "
          "or a callable (e.g. lambda) with signature `coasio::task<T>()`");
    }
  }

  template <typename Arg> auto block_on(Arg &&arg) {
    if constexpr (is_task_v<Arg>) {
      return _block_on(std::forward<Arg>(arg));
    } else if constexpr (std::invocable<Arg> &&
                         is_task_v<std::invoke_result_t<Arg>>) {
      return _block_on(std::forward<Arg>(arg)());
    } else {
      static_assert(
          sizeof(Arg) == 0,
          "runtime::block_on(F) requires F to be a coasio::task<T>, "
          "or a callable (e.g. lambda) with signature `coasio::task<T>()`");
    }
  }

  template <typename F> [[nodiscard]] auto spawn_blocking(F &&f) {
    return detail::blocking::blocking_awaiter<std::decay_t<F>>{
        blocking_pool(), std::forward<F>(f)};
  }

  asio::io_context &get_io_context() noexcept { return io_context_; }

  static asio::io_context &get_current_io_context() noexcept {
    auto *rt = current();
    if (!rt) {
      std::cerr << "Called outside a coasio runtime\n";
      std::terminate();
    }
    return rt->get_io_context();
  }

  void schedule(std::coroutine_handle<> h) {
    if (!h)
      return;
    put_task_in_queue(h);
  }

  // Queue
  std::optional<std::coroutine_handle<>> get_next_task_from_queue() {
    std::unique_lock lock(global_tasks_queue_mutex_);
    global_tasks_queue_cv_.wait(lock, [this] {
      return !global_tasks_.empty() ||
             stop_requested_.load(std::memory_order_acquire);
    });
    if (stop_requested_.load(std::memory_order_acquire) &&
        global_tasks_.empty()) {
      return std::nullopt;
    }
    auto h = global_tasks_.front();
    global_tasks_.pop();
    return h;
  }

  void put_task_in_queue(std::coroutine_handle<> h) {
    std::unique_lock lock(global_tasks_queue_mutex_);
    global_tasks_.push(h);
    global_tasks_queue_cv_.notify_one();
  }

  void register_root(detail::root_node *n) {
    std::lock_guard lock(roots_mutex_);
    n->next_ = roots_head_;
    n->prev_ = nullptr;
    if (roots_head_)
      roots_head_->prev_ = n;
    roots_head_ = n;
    ++live_roots_;
  }

  void unregister_root(detail::root_node *n) {
    std::lock_guard lock(roots_mutex_);
    if (n->prev_)
      n->prev_->next_ = n->next_;
    else
      roots_head_ = n->next_;
    if (n->next_)
      n->next_->prev_ = n->prev_;
    if (--live_roots_ == 0)
      roots_drained_cv_.notify_all();
  }

  friend class worker;
  friend class io_worker;
};

class runtime_builder {
public:
  runtime_builder() = default;

  runtime_builder &set_io_worker_count(const size_t count) {
    io_worker_count_ = count;
    return *this;
  }

  runtime_builder &set_worker_count(const size_t count) {
    worker_count_ = count;
    return *this;
  }

  runtime_builder &set_blocking_pool_size(const size_t size) {
    blocking_pool_size_ = size;
    return *this;
  }

  [[nodiscard]] runtime build() const {
    return runtime(io_worker_count_, worker_count_, blocking_pool_size_);
  }

private:
  size_t io_worker_count_ = 1;
  size_t worker_count_ = std::thread::hardware_concurrency() == 0
                             ? 1
                             : std::thread::hardware_concurrency();
  size_t blocking_pool_size_ = 1;
};

namespace detail {
inline runtime *current_runtime() noexcept { return runtime::current(); }

inline void runtime_schedule(runtime *rt, std::coroutine_handle<> h) noexcept {
  rt->schedule(h);
}

inline void runtime_unregister_root(runtime *rt,
                                    detail::root_node *n) noexcept {
  rt->unregister_root(n);
}
} // namespace detail

template <typename T> class JoinHandle {
public:
  JoinHandle() = default;
  JoinHandle(runtime *rt, std::shared_ptr<cancel_scope> scope,
             sync::oneshot::receiver<detail::root_outcome<T>> rx)
      : rt_(rt), scope_(std::move(scope)), rx_(std::move(rx)) {}

  void abort() const {
    if (scope_)
      scope_->cancel();
  }

  [[nodiscard]] bool cancelled() const noexcept {
    return detail::already_cancelled(scope_);
  }

  task<std::expected<T, std::error_code>> join() {
    auto r = co_await std::move(rx_);
    if (!r)
      co_return std::unexpected(r.error());
    if (r->exception)
      std::rethrow_exception(r->exception);
    if constexpr (std::is_void_v<T>)
      co_return std::expected<void, std::error_code>{};
    else
      co_return std::move(*r->value);
  }

private:
  runtime *rt_ = nullptr;
  std::shared_ptr<cancel_scope> scope_;
  sync::oneshot::receiver<detail::root_outcome<T>> rx_;
};
}; // namespace coasio

#endif // !COASIO_RUNTIME_HPP
