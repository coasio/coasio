#ifndef COASIO_COMBINATORS_TIMEOUT_HPP
#define COASIO_COMBINATORS_TIMEOUT_HPP

#include <chrono>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "coasio.hpp"
#include "coasio/combinators/select.hpp"
#include "coasio/detail/combinator_common.hpp"
#include "coasio/time.hpp"

namespace coasio::combinators {

template <typename Rep, typename Period, coasio_awaitable Aw>
auto timeout(std::chrono::duration<Rep, Period> duration, Aw &&op)
    -> task<std::expected<coasio::detail::unwrap_expected_ec_t<
                              coasio::detail::await_result_t<Aw>>,
                          std::error_code>> {
  using raw_result_t = coasio::detail::await_result_t<Aw>;
  using value_t = coasio::detail::unwrap_expected_ec_t<raw_result_t>;
  using return_t = std::expected<value_t, std::error_code>;

  auto sel_res = co_await select(std::forward<Aw>(op), time::sleep(duration));

  if (!sel_res.has_value()) {
    co_return std::unexpected(sel_res.error());
  }

  if (sel_res->index() == 0) {
    if constexpr (std::is_void_v<raw_result_t>) {
      co_return return_t{};
    } else if constexpr (coasio::detail::is_expected_ec_v<raw_result_t>) {
      co_return std::move(std::get<0>(*sel_res).value);
    } else {
      co_return return_t{std::in_place, std::move(std::get<0>(*sel_res).value)};
    }
  }

  auto &timer_branch = std::get<1>(*sel_res);
  if (!timer_branch.value.has_value()) {
    co_return std::unexpected(timer_branch.value.error());
  }

  co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

template <coasio_awaitable Aw, typename Rep, typename Period>
auto timeout(Aw &&op, std::chrono::duration<Rep, Period> duration)
    -> task<std::expected<coasio::detail::unwrap_expected_ec_t<
                              coasio::detail::await_result_t<Aw>>,
                          std::error_code>> {
  return timeout(duration, std::forward<Aw>(op));
}

} // namespace coasio::combinators

#endif // !COASIO_COMBINATORS_TIMEOUT_HPP