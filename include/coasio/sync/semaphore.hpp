#ifndef COASIO_SYNC_SEMAPHORE_HPP
#define COASIO_SYNC_SEMAPHORE_HPP

#include <cassert>
#include <coroutine>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>

#include "coasio/cancel_scope.hpp"
#include "coasio/detail/fwd.hpp"
#include "coasio/error.hpp"

namespace coasio::sync {
namespace semaphore_detail {

struct waiter {
  waiter *prev{nullptr};
  waiter *next{nullptr};
  std::coroutine_handle<> handle{};
  runtime *rt{nullptr};
  ptrdiff_t requested_permits{1};
  bool acquired{false};
  bool cancelled{false};
};

struct state {
  static constexpr size_t MAX_PERMITS =
      static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());

  std::atomic<ptrdiff_t> permits_;
  std::atomic<bool> has_waiters_{false};
  std::mutex queue_mtx_;
  waiter *head_{nullptr};
  waiter *tail_{nullptr};

  explicit state(const size_t initial) noexcept
      : permits_(static_cast<ptrdiff_t>(initial)) {
    assert(initial <= MAX_PERMITS && "Semaphore permit count exceeded MAX_PERMITS");
  }

  state(const state &) = delete;
  state &operator=(const state &) = delete;


  [[nodiscard]] bool try_acquire(size_t n) noexcept {
    if (n == 0) return true;
    std::lock_guard lk(queue_mtx_);
    if (head_ != nullptr) return false;
    const auto need = static_cast<ptrdiff_t>(n);
    auto cur = permits_.load(std::memory_order_relaxed);
    while (cur >= need) {
      if (permits_.compare_exchange_weak(cur, cur - need,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed))
        return true;
    }
    return false;
  }

  [[nodiscard]] bool try_acquire_or_enqueue(waiter *w) noexcept {
    to_wake buf[16];
    size_t n;
    bool acquired;
    {
      std::lock_guard lk(queue_mtx_);
      link_tail(w);
      n = grant_locked(buf, 16, w);
      acquired = w->acquired;
    }
    for (size_t i = 0; i < n; ++i)
      coasio::detail::runtime_schedule(buf[i].rt, buf[i].h);
    return acquired;
  }

  bool try_cancel_waiter(waiter *w) noexcept {
    std::lock_guard lk(queue_mtx_);
    if (w->acquired || w->cancelled) return false;
    unlink(w);
    w->cancelled = true;
    return true;
  }

  void release(size_t n = 1) noexcept {
    if (n == 0) return;
    permits_.fetch_add(static_cast<ptrdiff_t>(n), std::memory_order_seq_cst);
    if (!has_waiters_.load(std::memory_order_seq_cst)) return;

    to_wake buf[16];
    size_t count;
    {
      std::lock_guard lk(queue_mtx_);
      count = grant_locked(buf, 16);
    }
    for (size_t i = 0; i < count; ++i)
      coasio::detail::runtime_schedule(buf[i].rt, buf[i].h);
  }

  void dequeue_or_return(waiter *w) noexcept {
    to_wake buf[16];
    size_t n = 0;
    {
      std::lock_guard lk(queue_mtx_);
      if (w->cancelled) return;
      if (!w->acquired) {
        unlink(w);
      } else {
        permits_.fetch_add(w->requested_permits, std::memory_order_seq_cst);
        n = grant_locked(buf, 16);
      }
    }
    for (size_t i = 0; i < n; ++i)
      coasio::detail::runtime_schedule(buf[i].rt, buf[i].h);
  }

  [[nodiscard]] size_t available_permits() const noexcept {
    return static_cast<size_t>(permits_.load(std::memory_order_seq_cst));
  }

private:
  struct to_wake {
    runtime *rt{};
    std::coroutine_handle<> h;
  };

  void link_tail(waiter *w) noexcept {
    w->next = nullptr;
    w->prev = tail_;
    if (tail_) tail_->next = w; else head_ = w;
    tail_ = w;
    has_waiters_.store(true, std::memory_order_seq_cst);
  }

  void unlink(waiter *w) noexcept {
    if (w->prev) w->prev->next = w->next; else head_ = w->next;
    if (w->next) w->next->prev = w->prev; else tail_ = w->prev;
    w->prev = w->next = nullptr;
    if (!head_) has_waiters_.store(false, std::memory_order_seq_cst);
  }

  size_t grant_locked(to_wake *out, size_t cap,
                      const waiter *self = nullptr) noexcept {

    size_t n = 0;
    while (head_) {
      const auto need = head_->requested_permits;
      auto cur = permits_.load(std::memory_order_seq_cst);
      if (cur < need) break;
      if (!permits_.compare_exchange_weak(cur, cur - need,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed))
        continue;
      waiter *w = head_;
      unlink(w);
      w->acquired = true;
      if (w == self) continue;
      if (n < cap) out[n++] = {w->rt, w->handle};
      else         coasio::detail::runtime_schedule(w->rt, w->handle);
    }
    return n;
  }
};

}

class semaphore_permit {
public:
  semaphore_permit() = default;

  semaphore_permit(std::shared_ptr<semaphore_detail::state> sem,
                   size_t count) noexcept
      : sem_(std::move(sem)), permits_(count) {}

  semaphore_permit(semaphore_permit &&o) noexcept
      : sem_(std::move(o.sem_)), permits_(std::exchange(o.permits_, 0)) {}

  semaphore_permit &operator=(semaphore_permit &&o) noexcept {
    if (this != &o) {
      release_if_held();
      sem_ = std::move(o.sem_);
      permits_ = std::exchange(o.permits_, 0);
    }
    return *this;
  }

  semaphore_permit(const semaphore_permit &) = delete;
  semaphore_permit &operator=(const semaphore_permit &) = delete;

  ~semaphore_permit() { release_if_held(); }

  void forget() noexcept { permits_ = 0; }

  void merge(semaphore_permit &other) noexcept {
    assert(sem_ == other.sem_ && "Cannot merge permits from different semaphores");
    permits_ += std::exchange(other.permits_, 0);
  }

  [[nodiscard]] size_t count() const noexcept { return permits_; }
  [[nodiscard]] explicit operator bool() const noexcept { return permits_ > 0; }

private:
  void release_if_held() noexcept {
    if (sem_ && permits_ > 0) sem_->release(permits_);
  }

  std::shared_ptr<semaphore_detail::state> sem_;
  size_t permits_{0};
};

class semaphore {
public:
  static constexpr size_t MAX_PERMITS = semaphore_detail::state::MAX_PERMITS;

  explicit semaphore(size_t initial_permits = 0) noexcept
      : st_(std::make_shared<semaphore_detail::state>(initial_permits)) {}


  [[nodiscard]] std::optional<semaphore_permit>
  try_acquire(size_t permits = 1) noexcept {
    if (permits > MAX_PERMITS) return std::nullopt;
    if (st_->try_acquire(permits)) return semaphore_permit{st_, permits};
    return std::nullopt;
  }

  struct acquire_awaiter {
    std::shared_ptr<semaphore_detail::state> st_;
    semaphore_detail::waiter w_;
    std::shared_ptr<cancel_scope> scope_;
    cancel_guard guard_;
    bool enqueued_{false};

    acquire_awaiter(std::shared_ptr<semaphore_detail::state> st,
                    size_t n) noexcept
        : st_(std::move(st)) {
      w_.requested_permits = static_cast<ptrdiff_t>(n);
    }

    ~acquire_awaiter() {
      if (enqueued_) st_->dequeue_or_return(&w_);
    }

    void set_cancel_scope(std::shared_ptr<cancel_scope> s) noexcept {
      guard_.bind(scope_ = std::move(s));
    }

    bool await_ready() noexcept {
      if (coasio::detail::already_cancelled(scope_)) return true;
      if (st_->try_acquire(static_cast<size_t>(w_.requested_permits))) {
        w_.acquired = true;
        return true;
      }
      return false;
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
      w_.handle = h;
      w_.rt     = coasio::detail::current_runtime();

      auto r = guard_.arm([this] {
        if (st_->try_cancel_waiter(&w_))
          coasio::detail::runtime_schedule(w_.rt, w_.handle);
      });
      if (r == cancel_guard::arm_result::already_cancelled) return false;

      enqueued_ = true;
      if (st_->try_acquire_or_enqueue(&w_)) {
        enqueued_ = false;
        return false;
      }

      if (w_.cancelled) {
        enqueued_ = false;
        return false;
      }

      return true;
    }

    std::expected<semaphore_permit, std::error_code> await_resume() noexcept {
      guard_.disarm();
      enqueued_ = false;
      if (w_.acquired)
        return semaphore_permit{st_, static_cast<size_t>(w_.requested_permits)};
      return std::unexpected(coasio::error::cancelled);
    }
  };

  [[nodiscard]] acquire_awaiter acquire(size_t permits = 1) noexcept {
    assert(permits <= MAX_PERMITS && "semaphore::acquire: permits exceeds MAX_PERMITS");
    return acquire_awaiter{st_, permits};
  }

  void release(size_t permits = 1) noexcept { st_->release(permits); }

  [[nodiscard]] size_t available_permits() const noexcept {
    return st_->available_permits();
  }

private:
  std::shared_ptr<semaphore_detail::state> st_;
};

}

#endif // !COASIO_SYNC_SEMAPHORE_HPP
