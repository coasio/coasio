#ifndef COASIO_COMBINATORS_JOIN_HPP
#define COASIO_COMBINATORS_JOIN_HPP

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#include "coasio.hpp"
#include "coasio/detail/combinator_common.hpp"
#include "coasio/sync/oneshot.hpp"

namespace coasio::combinators {

namespace detail {

enum class join_status : std::uint8_t {
  pending,
  completed,
  failed_exception,
  aborted
};

template <typename... Ts> struct join_state {
  static constexpr std::size_t N = sizeof...(Ts);

  std::mutex mtx;
  join_status status = join_status::pending;
  std::size_t remaining = N;
  // TODO: If an awaitable returns T&, std::optional<T&> will not work.
  std::tuple<std::optional<coasio::detail::norm_void_t<Ts>>...> results;
  std::exception_ptr exception = nullptr;
  sync::oneshot::sender<void> tx;

  explicit join_state(sync::oneshot::sender<void> sender) noexcept
      : tx(std::move(sender)) {}

  template <std::size_t I, typename... Args> void set_value(Args &&...args) {
    bool notify = false;
    {
      std::lock_guard lk(mtx);
      if (status != join_status::pending)
        return;

      if constexpr (sizeof...(Args) == 0) {
        std::get<I>(results).emplace(std::monostate{});
      } else {
        std::get<I>(results).emplace(std::forward<Args>(args)...);
      }

      if (--remaining == 0) {
        status = join_status::completed;
        notify = true;
      }
    }
    if (notify) {
      auto _ = tx.send();
    }
  }

  void set_exception(const std::exception_ptr &ep) noexcept {
    {
      std::lock_guard lk(mtx);
      if (status != join_status::pending)
        return;
      exception = ep;
      status = join_status::failed_exception;
    }
    auto _ = tx.send();
  }

  bool try_mark_aborted() noexcept {
    std::lock_guard lk(mtx);
    if (status != join_status::pending)
      return false;
    status = join_status::aborted;
    return true;
  }
};

template <std::size_t I, coasio_awaitable Aw, typename State>
task<void> join_worker(Aw &&aw, std::shared_ptr<State> state) {
  using T = await_result_t<Aw>;
  try {
    if constexpr (std::is_void_v<T>) {
      co_await std::forward<Aw>(aw);
      state->template set_value<I>();
    } else {
      T val = co_await std::forward<Aw>(aw);
      state->template set_value<I>(std::move(val));
    }
  } catch (...) {
    state->set_exception(std::current_exception());
  }
  co_return;
}

template <typename Ret, coasio_awaitable... Aws, std::size_t... Is>
auto join_impl(std::index_sequence<Is...>, Aws &&...aws) -> task<Ret> {
  constexpr std::size_t N = sizeof...(Aws);
  using state_t = join_state<await_result_t<Aws>...>;
  using tuple_t = coasio::detail::join_tuple_t<await_result_t<Aws>...>;

  auto [tx, rx] = sync::make_oneshot<void>();
  auto state = std::make_shared<state_t>(std::move(tx));

  std::array<JoinHandle<void>, N> handles;
  coasio::detail::handles_abort_guard<N> abort_guard{handles};

  ((handles[Is] =
        coasio::spawn(join_worker<Is>(std::forward<Aws>(aws), state))),
   ...);

  auto wait_res = co_await std::move(rx);
  if (!wait_res.has_value()) {
    state->try_mark_aborted();
  }

  join_status status_snapshot;
  {
    std::lock_guard lk(state->mtx);
    status_snapshot = state->status;
  }

  if (status_snapshot != join_status::completed) {
    for (auto &h : handles) {
      h.abort();
    }
  }

  for (auto &h : handles) {
    auto _ = co_await h.join();
  }
  abort_guard.disarm();

  std::unique_lock lk(state->mtx);
  if (state->status == join_status::failed_exception) {
    auto ep = state->exception;
    lk.unlock();
    std::rethrow_exception(ep);
  }
  if (state->status != join_status::completed) {
    lk.unlock();
    co_return std::unexpected(coasio::error::cancelled);
  }

  tuple_t out{std::move(*std::get<Is>(state->results))...};
  lk.unlock();
  co_return std::move(out);
}

} // namespace detail

/// @brief Joins all supplied awaitables, waiting for all of them to complete.
/// Waits for all branches, unless a branch throws an exception
/// @note The returned task should be awaited immediately. Do not store it for
/// later use,
///       as the supplied awaitables must remain alive until the operation
///       completes.
template <coasio_awaitable... Aws> auto join(Aws &&...aws) {
  static_assert(sizeof...(Aws) > 0, "join requires at least one task");
  using ret_t =
      std::expected<coasio::detail::join_tuple_t<await_result_t<Aws>...>,
                    std::error_code>;
  return detail::join_impl<ret_t>(std::index_sequence_for<Aws...>{},
                                  std::forward<Aws>(aws)...);
}

} // namespace coasio::combinators

#endif // !COASIO_COMBINATORS_JOIN_HPP