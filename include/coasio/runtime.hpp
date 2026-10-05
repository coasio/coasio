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
#include <queue>
#include <ranges>
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
  constexpr static size_t max_local_tasks_ = 256;
  constexpr static size_t global_poll_interval_ = 60;
  runtime *rt_;
  std::array<std::coroutine_handle<>, max_local_tasks_> local_queue_;
  size_t local_queue_head_ = 0;
  size_t local_queue_tail_ = 0;
  std::mutex local_queue_mutex_;
  size_t next_victim_index_ = 0;

  std::mutex park_mutex_;
  std::condition_variable park_cv_;
  std::atomic<bool> parked_{false};

  static inline thread_local worker *current_worker_ = nullptr;

  /**
   *
   * @return std::coroutine_handle<> if a task was found, or nullptr if the
   * runtime is stopping
   */
  std::coroutine_handle<> try_pop_local_task() {
    std::lock_guard lock(local_queue_mutex_);
    if (local_queue_size() == 0)
      return {};

    const size_t pop_idx = (local_queue_tail_ == 0) ? max_local_tasks_ - 1 : local_queue_tail_ - 1;
    const auto h = local_queue_[pop_idx];
    local_queue_tail_ = pop_idx;
    return h;
  }

  bool try_push_local_task(std::coroutine_handle<> h) {
    std::lock_guard lock(local_queue_mutex_);
    size_t next_tail = local_queue_tail_ + 1;
    if (next_tail >= max_local_tasks_) {
      next_tail = 0;
    }

    if (next_tail == local_queue_head_) {
      return false;
    }

    local_queue_[local_queue_tail_] = h;
    local_queue_tail_ = next_tail;
    return true;
  }

  /**
   * This function assumes the stealer's local queue is empty and will override
   * it. The stealer is supposed to acquire its internal queue mutex for the
   * lifetime of the call.
   *
   * This function will not force a steal if it can't acquire the queue mutex in
   * the first try and will return false
   *
   * @param stealer a pointer to the worker who is performing the action
   * @return true if tasks were successfully stolen, false otherwise
   */
  [[nodiscard]] bool try_steal_into(worker *stealer) {
    const std::unique_lock lock(local_queue_mutex_, std::try_to_lock);
    if (!lock.owns_lock())
      return false;
    if (local_queue_size() != 0)
      return false;

    const size_t count = local_queue_size();
    if (count == 0)
      return false;

    const size_t to_steal = std::max<size_t>(1, count / 2);

    stealer->local_queue_head_ = 0;
    stealer->local_queue_tail_ = to_steal;

    size_t current_head = local_queue_head_;
    for (size_t i = 0; i < to_steal; i++) {
      stealer->local_queue_[i] = local_queue_[current_head];
      current_head++;
      if (current_head >= max_local_tasks_) {
        current_head = 0;
      }
    }
    local_queue_head_ = current_head;
    return true;
  }

  std::coroutine_handle<> try_steal_from_peers();

  /**
   * This function assumes the mutex is locked.
   */
  [[nodiscard]] size_t local_queue_size() const {
    if (local_queue_tail_ >= local_queue_head_) {
      return local_queue_tail_ - local_queue_head_;
    }
    return (max_local_tasks_ - local_queue_head_) + local_queue_tail_;
  }

  bool unpark() {
    if (parked_.exchange(false, std::memory_order_seq_cst)) {
      {
        std::lock_guard lock(park_mutex_);
      }
      park_cv_.notify_one();
      return true;
    }
    return false;
  }

  void park();

public:
  explicit worker(runtime *runtime) : rt_(runtime) {}

  void run();

  struct context_guard {
    worker *prev_;
    explicit context_guard(worker *w) noexcept : prev_(current_worker_) {
      current_worker_ = w;
    }
    ~context_guard() noexcept { current_worker_ = prev_; }
  };

  static worker *current() noexcept { return current_worker_; }

  friend class runtime;
};

class io_worker {
  runtime *runtime_;

public:
  explicit io_worker(runtime *runtime) : runtime_(runtime) {}

  void run() const;

  friend class runtime;
};

class runtime {
  std::queue<std::coroutine_handle<>> global_tasks_;
  std::mutex global_tasks_queue_mutex_;
  std::atomic<bool> stop_requested_{false};
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::vector<std::thread> io_worker_threads_;
  std::vector<std::pair<std::thread, std::unique_ptr<worker>>> worker_threads_;

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
    assert(worker::current() == nullptr && "Do not call block_on from inside a worker thread. Use co_await instead.");
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

  auto get_workers() { return worker_threads_ | std::views::values; }

  void notify_one_idle_worker() {
    for (const auto &w : get_workers()) {
      if (w->unpark()) {
        return;
      }
    }
  }

  // Queue
  std::coroutine_handle<> try_get_next_task_from_queue() {
    std::unique_lock lock(global_tasks_queue_mutex_);
    if (global_tasks_.empty()) {
      return {};
    }
    auto h = global_tasks_.front();
    global_tasks_.pop();
    return h;
  }

  void put_task_in_queue(std::coroutine_handle<> h) {
    {
      std::lock_guard lock(global_tasks_queue_mutex_);
      global_tasks_.push(h);
    }
    notify_one_idle_worker();
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

public:
  explicit runtime(size_t io_worker_count = 1, size_t worker_count = 0,
                   size_t blocking_pool_size = 1);

  ~runtime();

  static runtime *current() noexcept { return current_runtime_; }

  static worker *current_worker() noexcept { return worker::current(); }

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
    if (!h) {
      return;
    }

    if (auto* w = worker::current(); w) {
      if (w->try_push_local_task(h)) {
        w->unpark();
        return;
      }
    }

    put_task_in_queue(h);
  }

  void schedule_in(worker *w, std::coroutine_handle<> h) {
    if (!h) {
      return;
    }
    if (w->try_push_local_task(h)) {
      w->unpark();
    } else {
      schedule(h);
    }
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

inline worker *current_worker() noexcept { return runtime::current_worker(); }

inline void runtime_schedule(runtime *rt, const std::coroutine_handle<> h) {
  rt->schedule(h);
}

inline void runtime_schedule_in(runtime *rt, worker *w,
                                const std::coroutine_handle<> h) {
  rt->schedule_in(w, h);
}

inline void runtime_unregister_root(runtime *rt, detail::root_node *n) {
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
