#ifndef COASIO_DETAIL_COMBINATOR_COMMON_HPP
#define COASIO_DETAIL_COMBINATOR_COMMON_HPP

#include <array>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "coasio/runtime.hpp"

namespace coasio::detail {

// Resolves the awaiter object for any type supporting co_await
template <typename Aw>
constexpr decltype(auto)
get_awaiter(Aw &&aw) noexcept(noexcept(std::forward<Aw>(aw))) {
  if constexpr (requires { std::forward<Aw>(aw).operator co_await(); }) {
    return std::forward<Aw>(aw).operator co_await();
  } else if constexpr (requires { operator co_await(std::forward<Aw>(aw)); }) {
    return operator co_await(std::forward<Aw>(aw));
  } else {
    return std::forward<Aw>(aw);
  }
}

template <typename Aw>
concept awaitable = requires(Aw &&aw, std::coroutine_handle<> h) {
  {
    get_awaiter(std::forward<Aw>(aw)).await_ready()
  } -> std::convertible_to<bool>;
  { get_awaiter(std::forward<Aw>(aw)).await_suspend(h) };
  { get_awaiter(std::forward<Aw>(aw)).await_resume() };
} && requires(Aw &&aw, std::coroutine_handle<> h) {
  requires(
      std::same_as<decltype(get_awaiter(std::forward<Aw>(aw))
                                .await_suspend(
                                    std::declval<std::coroutine_handle<>>())),
                   void> ||
      std::same_as<decltype(get_awaiter(std::forward<Aw>(aw))
                                .await_suspend(
                                    std::declval<std::coroutine_handle<>>())),
                   bool> ||
      std::convertible_to<
          decltype(get_awaiter(std::forward<Aw>(aw))
                       .await_suspend(std::declval<std::coroutine_handle<>>())),
          std::coroutine_handle<>>);
};

template <awaitable Aw>
using await_result_t = decltype(get_awaiter(std::declval<Aw>()).await_resume());

template <typename Aw>
concept coasio_awaitable =
    awaitable<Aw> && (requires(Aw &aw, std::shared_ptr<cancel_scope> scope) {
      aw.set_cancel_scope(scope);
    } || is_task_v<Aw>);

// Result traits

template <typename T>
using norm_void_t = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

template <typename T> struct is_expected_ec : std::false_type {};

template <typename U>
struct is_expected_ec<std::expected<U, std::error_code>> : std::true_type {};

template <typename T>
inline constexpr bool is_expected_ec_v =
    is_expected_ec<std::remove_cvref_t<T>>::value;

template <typename T> struct unwrap_expected_ec {
  using type = T;
};

template <typename T>
struct unwrap_expected_ec<std::expected<T, std::error_code>> {
  using type = T;
};

template <typename T>
using unwrap_expected_ec_t = unwrap_expected_ec<std::remove_cvref_t<T>>::type;

template <typename... Ts> struct race_value {
  using type = std::common_type_t<Ts...>;
};

template <typename... Ts> using race_value_t = race_value<Ts...>::type;

// RAII guards

template <std::size_t N> struct handles_abort_guard {
  std::array<JoinHandle<void>, N> &handles;
  bool active = true;

  ~handles_abort_guard() {
    if (active) {
      for (auto &h : handles) {
        h.abort();
      }
    }
  }
  void disarm() noexcept { active = false; }
};

struct vector_handles_abort_guard {
  std::vector<JoinHandle<void>> &handles;
  bool active = true;

  ~vector_handles_abort_guard() {
    if (active) {
      for (auto &h : handles) {
        h.abort();
      }
    }
  }
  void disarm() noexcept { active = false; }
};

}; // namespace coasio::detail

namespace coasio {

using detail::await_result_t;
using detail::awaitable;
using detail::coasio_awaitable;

} // namespace coasio

#endif // !COASIO_DETAIL_COMBINATOR_COMMON_HPP