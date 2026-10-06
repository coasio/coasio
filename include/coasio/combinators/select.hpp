#ifndef COASIO_COMBINATORS_SELECT_HPP
#define COASIO_COMBINATORS_SELECT_HPP

#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#include "coasio.hpp"
#include "coasio/detail/combinator_common.hpp"
#include "coasio/sync/oneshot.hpp"

namespace coasio {

template <std::size_t Index, typename T> struct branch_result {
  static constexpr std::size_t index = Index;
  T value;
};

template <std::size_t Index> struct branch_result<Index, void> {
  static constexpr std::size_t index = Index;
};

namespace detail {

template <typename Seq, typename... Ts> struct select_variant_helper;

template <std::size_t... Is, typename... Ts>
struct select_variant_helper<std::index_sequence<Is...>, Ts...> {
  using type = std::variant<branch_result<Is, Ts>...>;
};

template <typename... Ts>
using select_variant_t =
    select_variant_helper<std::index_sequence_for<Ts...>, Ts...>::type;

// Awaiter that deliberately does NOT bind to parent cancellation scope,
// ensuring cleanup tasks can wait for background workers to finish.
// TODO: this can be refactored into a generic utility
struct uncancelled_waiter {
  std::shared_ptr<sync::oneshot::detail::state<void>> state_;

  explicit uncancelled_waiter(
      std::shared_ptr<sync::oneshot::detail::state<void>> s) noexcept
      : state_(std::move(s)) {}

  void set_cancel_scope(std::shared_ptr<cancel_scope>) noexcept {}

  bool await_ready() noexcept {
    if (!state_)
      return true;
    std::lock_guard lock(state_->mtx_);
    return state_->ready_ || state_->sender_dropped_;
  }

  bool await_suspend(std::coroutine_handle<> h) noexcept {
    std::lock_guard lock(state_->mtx_);
    if (state_->ready_ || state_->sender_dropped_)
      return false;
    state_->waiter_ = h;
    state_->rt_ = current_runtime();
    state_->w_ = current_worker();
    return true;
  }

  void await_resume() noexcept {}
};

template <typename... Ts> struct select_state {
  std::mutex mtx;
  bool completed = false;
  bool aborted = false;
  std::size_t winner_index = 0;
  std::exception_ptr exception = nullptr;

  using variant_t = select_variant_t<Ts...>;
  std::optional<variant_t> result;

  std::vector<JoinHandle<void>> handles;
  std::atomic<std::size_t> running_workers{sizeof...(Ts)};

  sync::oneshot::sender<void> tx;
  sync::oneshot::receiver<void> rx;
  std::shared_ptr<sync::oneshot::detail::state<void>> done_state;

  select_state() {
    auto [s, r] = sync::make_oneshot<void>();
    tx = std::move(s);
    rx = std::move(r);
    done_state = std::make_shared<sync::oneshot::detail::state<void>>();
  }

  void abort_all() {
    std::vector<std::size_t> to_abort;
    {
      std::lock_guard lk(mtx);
      aborted = true;
      to_abort.reserve(handles.size());
      for (std::size_t j = 0; j < handles.size(); ++j) {
        to_abort.push_back(j);
      }
    }
    for (std::size_t j : to_abort) {
      handles[j].abort();
    }
  }

  template <std::size_t I, typename... Args> void set_winner(Args &&...args) {
    std::vector<std::size_t> to_abort;
    {
      std::lock_guard lk(mtx);
      if (completed || aborted)
        return;

      if constexpr (sizeof...(Args) > 0) {
        static_assert(sizeof...(Args) == 1,
                      "set_winner accepts exactly one value");
        using value_t =
            std::remove_cvref_t<std::tuple_element_t<0, std::tuple<Args...>>>;
        result.emplace(std::in_place_type<branch_result<I, value_t>>,
                       branch_result<I, value_t>{std::forward<Args>(args)...});
      } else {
        result.emplace(std::in_place_type<branch_result<I, void>>,
                       branch_result<I, void>{});
      }

      completed = true;
      winner_index = I;
      for (std::size_t j = 0; j < handles.size(); ++j) {
        if (j != I)
          to_abort.push_back(j);
      }
    }
    for (std::size_t j : to_abort) {
      handles[j].abort();
    }
    auto _ = tx.send();
  }

  void set_exception(const std::size_t index, const std::exception_ptr &ep) {
    std::vector<std::size_t> to_abort;
    {
      std::lock_guard lk(mtx);
      if (completed || aborted)
        return;
      completed = true;
      winner_index = index;
      exception = ep;
      for (std::size_t j = 0; j < handles.size(); ++j) {
        if (j != index)
          to_abort.push_back(j);
      }
    }
    for (std::size_t j : to_abort) {
      handles[j].abort();
    }
    auto _ = tx.send();
  }
};

template <std::size_t I, typename T, typename State>
task<void> select_worker(task<T> t, std::shared_ptr<State> state) {
  try {
    if constexpr (std::is_void_v<T>) {
      co_await std::move(t);
      state->template set_winner<I>();
    } else {
      T val = co_await std::move(t);
      state->template set_winner<I>(std::move(val));
    }
  } catch (...) {
    state->set_exception(I, std::current_exception());
  }

  if (--state->running_workers == 0) {
    auto _ = state->done_state->set_value();
  }
}

template <typename... Ts, std::size_t... Is>
auto select_impl(std::index_sequence<Is...>, task<Ts>... tasks)
    -> task<typename select_state<Ts...>::variant_t> {
  using state_t = select_state<Ts...>;
  using variant_t = typename state_t::variant_t;

  auto state = std::make_shared<state_t>();

  {
    std::lock_guard lk(state->mtx);
    (state->handles.push_back(
         coasio::spawn(select_worker<Is>(std::move(tasks), state))),
     ...);
  }

  auto parent_scope = co_await current_cancel_scope{};
  cancel_guard guard;
  guard.bind(parent_scope);
  guard.arm([st = state.get()] { st->abort_all(); });

  co_await std::move(state->rx);
  guard.disarm();

  // Wait for all workers to finish their unwind/cleanup before returning.
  co_await uncancelled_waiter{state->done_state};

  std::unique_lock lk(state->mtx);
  if (state->exception) {
    auto ep = state->exception;
    lk.unlock();
    std::rethrow_exception(ep);
  }
  if (state->aborted || !state->result.has_value()) {
    lk.unlock();
    throw std::system_error(coasio::error::cancelled);
  }
  auto out = std::move(*state->result);
  lk.unlock();
  co_return std::move(out);
}
} // namespace detail

template <typename... Ts> auto select(task<Ts>... tasks) {
  static_assert(sizeof...(Ts) > 0, "select requires at least one task");
  return detail::select_impl(std::index_sequence_for<Ts...>{},
                             std::move(tasks)...);
}

} // namespace coasio

#endif // !COASIO_COMBINATORS_SELECT_HPP