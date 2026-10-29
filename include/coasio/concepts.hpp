#ifndef COASIO_CONCEPTS_HPP
#define COASIO_CONCEPTS_HPP

#include <concepts>
#include <coroutine>
#include <memory>
#include <type_traits>

#include "coasio/task.hpp"

namespace coasio {

namespace detail {

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

} // namespace detail

template <typename Aw>
concept awaitable = requires(Aw &&aw, std::coroutine_handle<> h) {
  {
    detail::get_awaiter(std::forward<Aw>(aw)).await_ready()
  } -> std::convertible_to<bool>;
  { detail::get_awaiter(std::forward<Aw>(aw)).await_suspend(h) };
  { detail::get_awaiter(std::forward<Aw>(aw)).await_resume() };
} && requires(Aw &&aw, std::coroutine_handle<> h) {
  requires(
      std::same_as<decltype(detail::get_awaiter(std::forward<Aw>(aw))
                                .await_suspend(
                                    std::declval<std::coroutine_handle<>>())),
                   void> ||
      std::same_as<decltype(detail::get_awaiter(std::forward<Aw>(aw))
                                .await_suspend(
                                    std::declval<std::coroutine_handle<>>())),
                   bool> ||
      std::convertible_to<
          decltype(detail::get_awaiter(std::forward<Aw>(aw))
                       .await_suspend(std::declval<std::coroutine_handle<>>())),
          std::coroutine_handle<>>);
};

template <awaitable Aw>
using await_result_t =
    decltype(detail::get_awaiter(std::declval<Aw>()).await_resume());

template <typename Aw>
concept coasio_awaitable =
    awaitable<Aw> && (requires(Aw &aw, std::shared_ptr<cancel_scope> scope) {
      aw.set_cancel_scope(scope);
    } || is_task_v<Aw>);

template <typename Aw, typename ExpectedResult>
concept awaitable_of =
    coasio_awaitable<Aw> &&
    std::same_as<std::remove_cvref_t<await_result_t<Aw>>, ExpectedResult>;

} // namespace coasio

#endif // !COASIO_CONCEPTS_HPP