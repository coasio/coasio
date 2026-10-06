#ifndef COASIO_DETAIL_COMBINATOR_COMMON_HPP
#define COASIO_DETAIL_COMBINATOR_COMMON_HPP

#include <coroutine>
#include <memory>
#include <utility>
#include <variant>

#include "coasio/cancel_scope.hpp"

namespace coasio::detail {

// TODO: those types are general purpose helpers not tied to combinators
// specifically

struct current_cancel_scope {
  std::shared_ptr<cancel_scope> scope;
  void set_cancel_scope(std::shared_ptr<cancel_scope> s) noexcept {
    scope = std::move(s);
  }
  bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  [[nodiscard]] std::shared_ptr<cancel_scope> await_resume() noexcept {
    return std::move(scope);
  }
};

template <typename T> struct void_to_monostate {
  using type = T;
};

template <> struct void_to_monostate<void> {
  using type = std::monostate;
};

template <typename T> using void_to_monostate_t = void_to_monostate<T>::type;

}; // namespace coasio::detail

#endif // !COASIO_DETAIL_COMBINATOR_COMMON_HPP