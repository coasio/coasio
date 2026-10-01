#ifndef COASIO_DETAIL_BLOCKING_SPAWN_BLOCKING_HPP
#define COASIO_DETAIL_BLOCKING_SPAWN_BLOCKING_HPP

#include <atomic>
#include <coroutine>
#include <exception>
#include <expected>
#include <optional>

#include "coasio/cancel_scope.hpp"
#include "coasio/detail/fwd.hpp"
#include "coasio/detail/root_outcome.hpp"
#include "coasio/error.hpp"
#include "pool.hpp"

namespace coasio::detail::blocking {

template <typename T, typename F>
struct control_block final : control_block_base {
  F fn;
  runtime *rt;
  std::coroutine_handle<> h;
  std::atomic<bool> cancelled{false};
  root_outcome<T> outcome;

  control_block(F f, runtime *r, std::coroutine_handle<> handle)
      : fn(std::move(f)), rt(r), h(handle) {}

  void execute() override {
    try {
      if constexpr (std::is_void_v<T>)
        fn();
      else
        outcome.value.emplace(fn());
    } catch (...) {
      outcome.exception = std::current_exception();
    }
    runtime_schedule(rt, h);
  }

  void mark_cancelled_and_schedule() {
    cancelled.store(true, std::memory_order_release);
    runtime_schedule(rt, h);
  }
};

template <typename F> class blocking_awaiter {
  using T = std::invoke_result_t<F>;
  using cb_t = control_block<T, F>;

  F f_;
  pool *pool_;
  std::shared_ptr<cancel_scope> scope_;
  cancel_guard guard_;
  std::shared_ptr<cb_t> cb_;

public:
  blocking_awaiter(pool &p, F f) : f_(std::move(f)), pool_(&p) {}

  void set_cancel_scope(std::shared_ptr<cancel_scope> s) noexcept {
    guard_.bind(scope_ = std::move(s));
  }

  bool await_ready() noexcept { return scope_ && scope_->cancelled(); }

  bool await_suspend(std::coroutine_handle<> h) {
    runtime *rt = current_runtime();
    auto cb = std::make_shared<cb_t>(std::move(f_), rt, h);

    auto r = guard_.arm([pool = pool_, cb] {
      if (pool->try_cancel(cb))
        cb->mark_cancelled_and_schedule();
    });
    if (r == cancel_guard::arm_result::already_cancelled) {
      return false;
    }

    if (!pool_->enqueue(cb)) {
      return false;
    }
    cb_ = cb;

    if (already_cancelled(scope_)) {
      if (pool_->try_cancel(cb))
        cb->mark_cancelled_and_schedule();
    }
    return true;
  }

  std::expected<T, std::error_code> await_resume() {
    guard_.disarm();
    if (!cb_ || cb_->cancelled.load(std::memory_order_acquire))
      return std::unexpected(coasio::error::cancelled);
    if (cb_->outcome.exception)
      std::rethrow_exception(cb_->outcome.exception);
    if constexpr (std::is_void_v<T>)
      return std::expected<void, std::error_code>{};
    else
      return std::move(*cb_->outcome.value);
  }
};
} // namespace coasio::detail::blocking

#endif // !COASIO_DETAIL_BLOCKING_SPAWN_BLOCKING_HPP