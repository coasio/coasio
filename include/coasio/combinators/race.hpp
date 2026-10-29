#ifndef COASIO_COMBINATORS_RACE_HPP
#define COASIO_COMBINATORS_RACE_HPP

#include <exception>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "coasio/combinators/select.hpp"
#include "coasio/detail/combinator_common.hpp"

namespace coasio::combinators {

namespace detail {

template <typename Ret, coasio_awaitable... Aws>
auto race_impl(Aws &&...aws) -> task<Ret> {
  using value_t =
      coasio::detail::race_value_t<await_result_t<Aws>...>;
  using return_t = std::expected<value_t, std::error_code>;

  auto sel_res = co_await select(std::forward<Aws>(aws)...);
  if (!sel_res.has_value()) {
    co_return std::unexpected(sel_res.error());
  }

  if constexpr (std::is_void_v<value_t>) {
    co_return return_t{};
  } else {
    co_return std::visit(
        []<typename T>(T &&branch) -> return_t {
          return return_t{std::in_place, std::forward<T>(branch).value};
        },
        std::move(*sel_res));
  }
}

} // namespace detail

/// @brief Races multiple awaitables concurrently, returning the value of the
/// first to complete successfully.
/// @details If the first completed awaitable fails, or if an operation-wide
/// error occurs,
///          the failure is propagated as a std::error_code.
/// @note The returned task should be awaited immediately. Do not store it for
/// later use,
///       as the supplied awaitables must remain alive until the operation
///       completes.
template <coasio_awaitable... Aws> auto race(Aws &&...aws) {
  static_assert(sizeof...(Aws) > 0, "race requires at least one awaitable");
  using value_t =
      coasio::detail::race_value_t<await_result_t<Aws>...>;
  using ret_t = std::expected<value_t, std::error_code>;
  return detail::race_impl<ret_t>(std::forward<Aws>(aws)...);
}

} // namespace coasio::combinators

#endif // !COASIO_COMBINATORS_RACE_HPP
