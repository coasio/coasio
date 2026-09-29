#ifndef COASIO_TASK_HPP
#define COASIO_TASK_HPP

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "coasio/detail/root_node.hpp"
#include "coasio/detail/root_outcome.hpp"
#include "coasio/cancel_scope.hpp"
#include "coasio/sync/oneshot.hpp"

namespace coasio {
template <typename T> class task {
public:
  struct promise_type : detail::root_node {
    std::optional<T> result_;
    std::exception_ptr exception_;
    std::shared_ptr<cancel_scope> owning_scope_;
    sync::oneshot::sender<detail::root_outcome<T>> result_sender_;
    std::coroutine_handle<> continuation_;
    bool detached_ = false;

    root_node *get_root_node() {
      return this;
    }

    task get_return_object() {
      return task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }

    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_type> h) noexcept {
          auto &p = h.promise();
          if (p.detached_) {
            detail::root_outcome<T> outcome;
            if (p.exception_) {
              outcome.exception = p.exception_;
            } else {
              outcome.value = std::move(p.result_);
            }
            // Not a problem if nobody's listening
            auto _ = p.result_sender_.send(std::move(outcome));
            if (p.owner_rt_) runtime_unregister_root(p.owner_rt_, &p);
            h.destroy();
            return std::noop_coroutine();
          }
          return (p.continuation_) ? p.continuation_ : std::noop_coroutine();
        }

        void await_resume() noexcept {}
      };
      return final_awaiter{};
    }

    template <typename U>
    void return_value(U &&value)
      requires std::is_convertible_v<U, T>
    {
      result_.emplace(std::forward<U>(value));
    }
    void unhandled_exception() { exception_ = std::current_exception(); }

    template <typename U> auto await_transform(task<U> &&child) {
      auto &cp = child.handle().promise();
      cp.owning_scope_ = owning_scope_;
      return std::move(child);
    }

    template <typename Awaitable>
    decltype(auto) await_transform(Awaitable &&a) {
      // Leaves (timers, sockets, etc.)
      if constexpr (requires { a.set_cancel_scope(owning_scope_); }) {
        a.set_cancel_scope(owning_scope_);
      } else {
        static_assert(sizeof(Awaitable) == 0, "This awaitable don't have set_cancel_scope");
      }
      return std::forward<Awaitable>(a);
    }
  };

  template <typename Self> auto operator co_await(this Self &&self) noexcept {
    static_assert(std::is_rvalue_reference_v<Self &&>,
                  "task must be co_awaited as a prvalue/rvalue, did you forget "
                  "std::move?");
    struct awaiter {
      std::coroutine_handle<promise_type> handle_;

      ~awaiter() {
        if (handle_)
          handle_.destroy();
      }

      bool await_ready() const noexcept { return !handle_ || handle_.done(); }

      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
      }

      T await_resume() {
        auto &p = handle_.promise();
        if (p.exception_)
          std::rethrow_exception(p.exception_);
        return std::move(*p.result_);
      }
    };
    return awaiter{std::exchange(self.handle_, nullptr)};
  }

  explicit task(std::coroutine_handle<promise_type> h) : handle_(h) {}

  task(task &&o) noexcept : handle_(std::exchange(o.handle_, {})) {}

  task& operator=(task &&o) noexcept {
    if (this != &o) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(o.handle_, {});
    }
    return *this;
  }

  task(const task &) = delete;

  task& operator=(const task &) = delete;

  ~task() {
    if (handle_)
      handle_.destroy();
  }

  [[nodiscard]] std::coroutine_handle<promise_type> release() noexcept {
    return std::exchange(handle_, nullptr);
  }

  [[nodiscard]] std::coroutine_handle<promise_type> detach() noexcept {
    auto h = release();
    if (h)
      h.promise().detached_ = true;
    return h;
  }

  void set_owning_scope(std::shared_ptr<cancel_scope> owning_scope) {
    handle_.promise().owning_scope_ = std::move(owning_scope);
  }

  [[nodiscard]] std::coroutine_handle<promise_type> handle() const noexcept {
    return handle_;
  }

  explicit operator bool() const { return handle_ && !handle_.done(); }

private:
  std::coroutine_handle<promise_type> handle_;
};

template <> class task<void> {
public:
  struct promise_type : detail::root_node {
    std::exception_ptr exception_;
    std::shared_ptr<cancel_scope> owning_scope_;
    sync::oneshot::sender<detail::root_outcome<void>> result_sender_;
    std::coroutine_handle<> continuation_;
    bool detached_ = false;

    root_node *get_root_node() {
      return this;
    }

    task get_return_object() {
      return task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }

    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_type> h) noexcept {
          auto &p = h.promise();
          if (p.detached_) {
            detail::root_outcome<void> outcome;
            if (p.exception_) {
              outcome.exception = p.exception_;
            }
            auto _ = p.result_sender_.send(std::move(outcome));
            if (p.owner_rt_) runtime_unregister_root(p.owner_rt_, &p);
            h.destroy();
            return std::noop_coroutine();
          }
          return (p.continuation_) ? p.continuation_ : std::noop_coroutine();
        }

        void await_resume() noexcept {}
      };
      return final_awaiter{};
    }

    void return_void() {}

    void unhandled_exception() { exception_ = std::current_exception(); }

    template <typename U> auto await_transform(task<U> &&child) {
      auto &cp = child.handle().promise();
      cp.owning_scope_ = owning_scope_;
      return std::move(child);
    }

    template <typename Awaitable>
    decltype(auto) await_transform(Awaitable &&a) {
      // Leaves (timers, sockets, etc.)
      if constexpr (requires { a.set_cancel_scope(owning_scope_); }) {
        a.set_cancel_scope(owning_scope_);
      } else {
        // TODO: To be replaced with a proper error mechanism that only triggers in library tests
        static_assert(sizeof(Awaitable) == 0, "This awaitable don't have set_cancel_scope");
      }
      return std::forward<Awaitable>(a);
    }
  };

  template <typename Self> auto operator co_await(this Self &&self) noexcept {
    static_assert(std::is_rvalue_reference_v<Self &&>,
                  "task must be co_awaited as a prvalue/rvalue, did you forget "
                  "std::move?");
    struct awaiter {
      std::coroutine_handle<promise_type> handle_;

      ~awaiter() {
        if (handle_)
          handle_.destroy();
      }

      bool await_ready() const noexcept { return !handle_ || handle_.done(); }

      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
      }

      void await_resume() const {
        auto &p = handle_.promise();
        if (p.exception_)
          std::rethrow_exception(p.exception_);
      }
    };
    return awaiter{std::exchange(self.handle_, nullptr)};
  }

  explicit task(std::coroutine_handle<promise_type> h) : handle_(h) {}

  task(task &&o) noexcept : handle_(std::exchange(o.handle_, {})) {}

  task& operator=(task &&o) noexcept {
    if (this != &o) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(o.handle_, {});
    }
    return *this;
  }

  task(const task &) = delete;

  task &operator=(const task &) = delete;

  ~task() {
    if (handle_)
      handle_.destroy();
  }

  [[nodiscard]] std::coroutine_handle<promise_type> release() noexcept {
    return std::exchange(handle_, nullptr);
  }

  [[nodiscard]] std::coroutine_handle<promise_type> detach() noexcept {
    auto h = release();
    if (h)
      h.promise().detached_ = true;
    return h;
  }

  void set_owning_scope(std::shared_ptr<cancel_scope> owning_scope) {
    handle_.promise().owning_scope_ = std::move(owning_scope);
  }

  [[nodiscard]] std::coroutine_handle<promise_type> handle() const noexcept {
    return handle_;
  }

  explicit operator bool() const { return handle_ && !handle_.done(); }

private:
  std::coroutine_handle<promise_type> handle_;
};

template <typename T> struct is_task : std::false_type {};
template <typename T> struct is_task<task<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_task_v = is_task<std::remove_cvref_t<T>>::value;
} // namespace coasio

#endif // !COASIO_TASK_HPP
