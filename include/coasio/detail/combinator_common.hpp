#ifndef COASIO_DETAIL_COMBINATOR_COMMON_HPP
#define COASIO_DETAIL_COMBINATOR_COMMON_HPP

#include <array>
#include <expected>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "coasio/runtime.hpp"

namespace coasio::detail {

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

template <typename... Ts> using join_tuple_t = std::tuple<norm_void_t<Ts>...>;

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

#endif // !COASIO_DETAIL_COMBINATOR_COMMON_HPP