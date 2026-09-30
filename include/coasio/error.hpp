#ifndef COASIO_ERROR_HPP
#define COASIO_ERROR_HPP

#include <asio/error.hpp>
#include <system_error>

namespace coasio {

enum class error {
  cancelled = 1,
  channel_closed,
  semaphore_closed,
};

namespace detail {
class error_category final : public std::error_category {
public:
  const char *name() const noexcept override { return "coasio"; }

  std::string message(int ev) const override {
    switch (static_cast<error>(ev)) {
    case error::cancelled:
      return "operation cancelled";
    case error::channel_closed:
      return "channel closed";
    case error::semaphore_closed:
      return "semaphore closed";
    default:
      return "unknown coasio error";
    }
  }

  std::error_condition default_error_condition(int ev) const noexcept override {
    if (static_cast<error>(ev) == error::cancelled)
      return std::errc::operation_canceled;
    return std::error_condition(ev, *this);
  }
};
} // namespace detail

inline const std::error_category &coasio_category() {
  static detail::error_category instance;
  return instance;
}

inline std::error_code make_error_code(error e) {
  return {static_cast<int>(e), coasio_category()};
}

using error_code = std::error_code;

} // namespace coasio

namespace std {
template <> struct is_error_code_enum<coasio::error> : true_type {};
} // namespace std

#endif // !COASIO_ERROR_HPP
