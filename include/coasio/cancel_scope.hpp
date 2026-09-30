#ifndef COASIO_CANCEL_SCOPE_HPP
#define COASIO_CANCEL_SCOPE_HPP

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace coasio {
class cancel_scope;

class cancel_guard {
public:
  enum class state : std::uint8_t { idle, armed, fired, disarmed };
  enum class arm_result : std::uint8_t { armed, already_cancelled };

  cancel_guard() = default;
  explicit cancel_guard(std::shared_ptr<cancel_scope> s) noexcept
      : scope_(std::move(s)) {}

  cancel_guard(const cancel_guard &) = delete;
  cancel_guard &operator=(const cancel_guard &) = delete;

  cancel_guard(cancel_guard &&o) noexcept {
    assert(o.state_.load(std::memory_order_relaxed) != state::armed &&
           "Cannot move an armed cancel_guard");
    scope_ = std::move(o.scope_);
    state_.store(o.state_.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
  }
  cancel_guard &operator=(cancel_guard &&o) noexcept {
    if (this != &o) {
      disarm();
      assert(o.state_.load(std::memory_order_relaxed) != state::armed &&
             "Cannot move an armed cancel_guard");
      scope_ = std::move(o.scope_);
      state_.store(o.state_.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
    }
    return *this;
  }

  ~cancel_guard() { disarm(); }

  void bind(std::shared_ptr<cancel_scope> s) noexcept {
    assert(state_.load(std::memory_order_relaxed) != state::armed);
    scope_ = std::move(s);
  }

  // f: void(). Runs under the scope mutex on the cancelling thread.
  // It must not call arm, disarm or cancel, and must not block. Take the
  // primitive's own lock, flip its state, and schedule a resume.
  template <class F> arm_result arm(F &&f);

  // true => was still armed; callback will never run.
  // false => never armed, already disarmed, or it fired (and has fully
  //          finished by the time this returns, since we take the same mutex).
  // Never call while holding a lock the callback takes (lock order is
  // scope.mtx -> primitive.mtx).
  bool disarm() noexcept;

  [[nodiscard]] bool fired() const noexcept {
    return state_.load(std::memory_order_acquire) == state::fired;
  }

private:
  friend class cancel_scope;
  std::shared_ptr<cancel_scope> scope_;
  std::move_only_function<void()> cb_;
  cancel_guard *prev_ = nullptr, *next_ = nullptr;
  std::atomic<state> state_{state::idle};
};

class cancel_scope {
public:
  cancel_scope() = default;
  cancel_scope(const cancel_scope &) = delete;
  cancel_scope &operator=(const cancel_scope &) = delete;
  ~cancel_scope() { cancel(); }

  bool cancelled() const noexcept {
    return cancelled_.load(std::memory_order_acquire);
  }

  void cancel() noexcept {
    if (cancelled_.load(std::memory_order_acquire))
      return;

    std::lock_guard lk(mtx_);
    if (cancelled_.exchange(true, std::memory_order_acq_rel))
      return;

    while (head_) {
      cancel_guard *g = head_;
      unlink_locked(g);
      g->state_.store(cancel_guard::state::fired, std::memory_order_release);
      auto cb = std::move(g->cb_);
      cb();
    }
  }

private:
  friend class cancel_guard;

  void link_locked(cancel_guard *g) noexcept {
    g->prev_ = nullptr;
    g->next_ = head_;
    if (head_)
      head_->prev_ = g;
    head_ = g;
  }

  void unlink_locked(cancel_guard *g) noexcept {
    if (g->prev_)
      g->prev_->next_ = g->next_;
    else
      head_ = g->next_;
    if (g->next_)
      g->next_->prev_ = g->prev_;
    g->prev_ = g->next_ = nullptr;
  }

  std::mutex mtx_;
  std::atomic<bool> cancelled_{false};
  cancel_guard *head_{nullptr};
};

template <class F> cancel_guard::arm_result cancel_guard::arm(F &&f) {
  assert(state_.load(std::memory_order_relaxed) == state::idle);
  if (!scope_) {
    state_.store(state::armed);
    return arm_result::armed;
  }
  std::lock_guard lk(scope_->mtx_);
  if (scope_->cancelled_.load(std::memory_order_relaxed)) {
    return arm_result::already_cancelled;
  }
  cb_ = std::forward<F>(f);
  scope_->link_locked(this);
  state_.store(state::armed, std::memory_order_release);
  return arm_result::armed;
}

inline bool cancel_guard::disarm() noexcept {
  if (!scope_) {
    return state_.exchange(state::disarmed) == state::armed;
  }

  std::lock_guard lk(scope_->mtx_);
  if (state_.load(std::memory_order_relaxed) != state::armed) {
    return false;
  }

  scope_->unlink_locked(this);
  cb_ = nullptr;
  state_.store(state::disarmed, std::memory_order_release);
  return true;
}

namespace detail {
inline bool
already_cancelled(const std::shared_ptr<cancel_scope> &scope) noexcept {
  return scope && scope->cancelled();
}
} // namespace detail
} // namespace coasio

#endif // !COASIO_CANCEL_SCOPE_HPP
