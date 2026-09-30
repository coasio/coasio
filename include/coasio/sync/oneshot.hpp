#ifndef COASIO_SYNC_ONESHOT_HPP
#define COASIO_SYNC_ONESHOT_HPP

#include <coroutine>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

#include "coasio/cancel_scope.hpp"
#include "coasio/detail/fwd.hpp"
#include "coasio/error.hpp"

namespace coasio::sync {

namespace oneshot {
namespace detail {
template <typename T> struct state {
  std::mutex mtx_;
  std::optional<T> value_;
  bool ready_{false};
  bool sender_dropped_{false};
  bool receiver_dropped_{false};
  bool cancelled_{false};
  std::coroutine_handle<> waiter_;
  runtime *rt_{nullptr};

  explicit state() noexcept = default;

  state(const state &) = delete;
  state &operator=(const state &) = delete;

  std::expected<void, T> set_value(T value) {
    std::coroutine_handle<> to_wake;
    {
      std::lock_guard lock(mtx_);
      if (ready_ || receiver_dropped_ || cancelled_) {
        return std::unexpected(std::move(value));
      }
      value_ = std::move(value);
      ready_ = true;
      to_wake = std::exchange(waiter_, nullptr);
    }
    if (to_wake)
      coasio::detail::runtime_schedule(rt_, to_wake);
    return {};
  }

  void mark_sender_dropped() {
    std::coroutine_handle<> to_wake;
    {
      std::lock_guard lock(mtx_);
      if (ready_ || sender_dropped_ || cancelled_)
        return; // already resolved
      sender_dropped_ = true;
      to_wake = std::exchange(waiter_, nullptr);
    }
    if (to_wake)
      coasio::detail::runtime_schedule(rt_, to_wake);
  }

  void mark_receiver_dropped() {
    std::lock_guard lock(mtx_);
    receiver_dropped_ = true;
  }

  bool is_closed() {
    std::lock_guard lock(mtx_);
    return receiver_dropped_ || cancelled_;
  }

  bool cancel_and_take_waiter() {
    std::lock_guard lock(mtx_);
    if (ready_ || sender_dropped_ || cancelled_)
      return false;
    cancelled_ = true;
    return std::exchange(waiter_, nullptr) != nullptr;
  }
};
} // namespace detail

template <typename T> class sender {
  std::shared_ptr<detail::state<T>> state_;

public:
  sender() = default;
  explicit sender(std::shared_ptr<detail::state<T>> s) noexcept
      : state_(std::move(s)) {}
  sender(sender &&) = default;
  sender &operator=(sender &&) = default;
  sender(const sender &) = delete;
  sender &operator=(const sender &) = delete;

  ~sender() {
    if (state_) {
      state_->mark_sender_dropped();
    }
  }

  std::expected<void, T> send(T value) {
    if (!state_)
      return std::unexpected(std::move(value));
    return state_->set_value(std::move(value));
  }

  [[nodiscard]] bool is_closed() const {
    return !state_ || state_->is_closed();
  }
};

template <typename T> class receiver {
  std::shared_ptr<detail::state<T>> state_;
  std::shared_ptr<cancel_scope> scope_;

public:
  receiver() = default;
  explicit receiver(std::shared_ptr<detail::state<T>> s) noexcept
      : state_(std::move(s)) {}
  receiver(receiver &&) = default;
  receiver &operator=(receiver &&) = default;
  receiver(const receiver &) = delete;
  receiver &operator=(const receiver &) = delete;

  ~receiver() {
    if (state_) {
      state_->mark_receiver_dropped();
    }
  }

  // See coasio::task::promise_type::await_transform(Awaitable &&a)
  void set_cancel_scope(std::shared_ptr<cancel_scope> s) noexcept {
    scope_ = std::move(s);
  }

  auto operator co_await() & = delete;

  auto operator co_await() && noexcept {
    if (state_)
      state_->rt_ = coasio::detail::current_runtime();

    struct awaiter {
      std::shared_ptr<detail::state<T>> state_;
      std::shared_ptr<cancel_scope> scope_;
      cancel_guard guard_;

      explicit awaiter(std::shared_ptr<detail::state<T>> s,
                       std::shared_ptr<cancel_scope> c) noexcept
          : state_(std::move(s)), scope_(std::move(c)) {
        guard_.bind(scope_);
      }

      ~awaiter() {
        if (state_) {
          state_->mark_receiver_dropped();
        }
      }

      bool await_ready() noexcept {
        if (coasio::detail::already_cancelled(scope_)) {
          return true;
        }
        std::lock_guard lock(state_->mtx_);
        return state_->ready_ || state_->sender_dropped_;
      }

      bool await_suspend(std::coroutine_handle<> h) noexcept {
        auto r = guard_.arm([st = state_.get(), h] {
          if (st->cancel_and_take_waiter())
            coasio::detail::runtime_schedule(st->rt_, h);
        });
        if (r == cancel_guard::arm_result::already_cancelled)
          return false;

        std::lock_guard lock(state_->mtx_);
        if (state_->ready_ || state_->sender_dropped_ || state_->cancelled_)
          return false;
        state_->waiter_ = h;
        return true;
      }

      std::expected<T, std::error_code> await_resume() {
        guard_.disarm();
        std::unique_lock lock(state_->mtx_);
        if (state_->ready_) {
          return std::move(*state_->value_);
        }
        if (state_->cancelled_ || coasio::detail::already_cancelled(scope_)) {
          return std::unexpected(coasio::error::cancelled);
        }
        return std::unexpected(coasio::error::channel_closed);
      }
    };

    return awaiter{std::exchange(state_, nullptr), std::move(scope_)};
  }

  [[nodiscard]] bool is_closed() const {
    return !state_ || state_->is_closed();
  }
};
} // namespace oneshot

template <typename T>
std::pair<oneshot::sender<T>, oneshot::receiver<T>> make_oneshot() {
  auto state = std::make_shared<oneshot::detail::state<T>>();
  return {oneshot::sender<T>(state), oneshot::receiver<T>(state)};
}
} // namespace coasio::sync

#endif // !COASIO_SYNC_ONESHOT_HPP
