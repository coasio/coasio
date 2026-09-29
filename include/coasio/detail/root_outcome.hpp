#ifndef COASIO_DETAIL_ROOT_OUTCOME_HPP
#define COASIO_DETAIL_ROOT_OUTCOME_HPP

#include <exception>
#include <optional>

namespace coasio::detail {
template <typename T>
struct root_outcome {
  std::optional<T> value;
  std::exception_ptr exception;
};

template <>
struct root_outcome<void> {
  std::exception_ptr exception;
};
}

#endif // !COASIO_DETAIL_ROOT_OUTCOME_HPP