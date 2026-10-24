#ifndef COASIO_COMBINATORS_SELECT_HPP
#define COASIO_COMBINATORS_SELECT_HPP

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "coasio.hpp"
#include "coasio/detail/combinator_common.hpp"
#include "coasio/sync/oneshot.hpp"

namespace coasio::combinators {

template <std::size_t Index, typename T> struct branch_result {
  static constexpr std::size_t index = Index;
  T value;
};

template <std::size_t Index> struct branch_result<Index, void> {
  static constexpr std::size_t index = Index;
};

namespace detail {

template <typename Seq, typename... Aws> struct select_variant_helper;

template <std::size_t... Is, typename... Aws>
struct select_variant_helper<std::index_sequence<Is...>, Aws...> {
  using type = std::variant<branch_result<Is, Aws>...>;
};

template <typename... Aws>
using select_variant_t =
    select_variant_helper<std::index_sequence_for<Aws...>, Aws...>::type;

enum class select_status : std::uint8_t {
  pending,
  won_value,
  won_exception,
  aborted
};

template <typename... Aws> struct select_state {
  using variant_t = select_variant_t<Aws...>;
  using return_value_t = std::tuple<Aws...>;
  static constexpr std::size_t N = sizeof...(Aws);

  std::mutex mtx;
  select_status status = select_status::pending;
  std::size_t winner_index = 0;
  std::optional<variant_t> result;
  std::exception_ptr exception = nullptr;
  sync::oneshot::sender<void> tx;

  explicit select_state(sync::oneshot::sender<void> sender) noexcept
      : tx(std::move(sender)) {}

  template <std::size_t I, typename... Args> void set_winner(Args &&...args) {
    using value_t = std::tuple_element_t<I, return_value_t>;

    {
      std::lock_guard lk(mtx);
      if (status != select_status::pending)
        return;

      if constexpr (std::is_void_v<value_t>) {
        result.emplace(std::in_place_type<branch_result<I, void>>, branch_result<I, void>{});
      }
      else {
        static_assert(sizeof...(Args) == 1,
                      "set_winner accepts exactly one value");
        result.emplace(std::in_place_type<branch_result<I, value_t>>,
                       branch_result<I, value_t>{std::forward<Args>(args)...});
      }

      winner_index = I;
      status = select_status::won_value;
    }
    auto _ = tx.send();
  }

  void set_exception(const std::size_t idx, const std::exception_ptr &ep) {
    {
      std::lock_guard lk(mtx);
      if (status != select_status::pending)
        return;
      winner_index = idx;
      exception = ep;
      status = select_status::won_exception;
    }
    auto _ = tx.send();
  }

  bool try_mark_aborted() noexcept {
    std::lock_guard lk(mtx);
    if (status != select_status::pending)
      return false;
    status = select_status::aborted;
    return true;
  }
};

template <std::size_t I, coasio_awaitable Aw, typename State>
task<void> select_worker(Aw &&aw, std::shared_ptr<State> state) {
  using T = await_result_t<Aw>;
  try {
    if constexpr (std::is_void_v<T>) {
      co_await std::forward<Aw>(aw);
      state->template set_winner<I>();
    } else {
      T val = co_await std::forward<Aw>(aw);
      state->template set_winner<I>(std::move(val));
    }
  } catch (...) {
    state->set_exception(I, std::current_exception());
  }
  co_return;
}

template <coasio_awaitable... Aws, std::size_t... Is>
auto select_impl(std::index_sequence<Is...>, Aws &&...aws) -> task<
    std::expected<select_variant_t<await_result_t<Aws>...>, std::error_code>> {
  constexpr std::size_t N = sizeof...(Aws);
  using state_t = select_state<await_result_t<Aws>...>;

  auto [tx, rx] = sync::make_oneshot<void>();
  auto state = std::make_shared<state_t>(std::move(tx));

  std::array<JoinHandle<void>, N> handles;
  coasio::detail::handles_abort_guard<N> abort_guard{handles};

  ((handles[Is] =
        coasio::spawn(select_worker<Is>(std::forward<Aws>(aws), state))),
   ...);

  auto wait_res = co_await std::move(rx);
  if (!wait_res.has_value()) {
    state->try_mark_aborted();
  }

  select_status status_snapshot;
  std::size_t winner_idx = 0;
  {
    std::lock_guard lk(state->mtx);
    status_snapshot = state->status;
    winner_idx = state->winner_index;
  }

  for (std::size_t j = 0; j < N; ++j) {
    if (status_snapshot == select_status::aborted || j != winner_idx) {
      handles[j].abort();
    }
  }

  for (auto &h : handles) {
    auto _ = co_await h.join();
  }
  abort_guard.disarm();

  std::unique_lock lk(state->mtx);
  if (state->status == select_status::won_exception) {
    auto ep = state->exception;
    lk.unlock();
    std::rethrow_exception(ep);
  }
  if (state->status == select_status::aborted || !state->result.has_value()) {
    lk.unlock();
    co_return std::unexpected(coasio::error::cancelled);
  }

  auto out = std::move(*state->result);
  lk.unlock();
  co_return std::move(out);
}

} // namespace detail

/// @brief Selects the first awaitable to complete.
/// First branch to complete (value, or exception)
/// @note The returned task should be awaited immediately. Do not store it for
/// later use,
///       as the supplied awaitables must remain alive until the operation
///       completes.
template <coasio_awaitable... Aws> auto select(Aws &&...aws) {
  static_assert(sizeof...(Aws) > 0, "select requires at least one awaitable");
  return detail::select_impl(std::index_sequence_for<Aws...>{},
                             std::forward<Aws>(aws)...);
}

} // namespace coasio::combinators

#endif // !COASIO_COMBINATORS_SELECT_HPP