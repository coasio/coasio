#ifndef COASIO_SIGNAL_HPP
#define COASIO_SIGNAL_HPP

#include <cassert>
#include <csignal>
#include <expected>
#include <system_error>

#include <asio/signal_set.hpp>

#include "detail/async_op.hpp"
#include "detail/fwd.hpp"
#include "task.hpp"

namespace coasio {

class signal {
public:
  signal()
      : signals_(detail::get_current_io_context(detail::current_runtime())) {}

  signal(const signal &) = delete;
  signal &operator=(const signal &) = delete;

  void add(const int sig) { signals_.add(sig); }

  auto wait() {
    assert(signals_.size() > 0 &&
           "signal::wait() called with no signals registered");
    return detail::async_op<int>([this]<typename Args>(Args &&token) {
      asio_handle().async_wait(std::forward<Args>(token));
    });
  }

  static task<std::expected<int, std::error_code>> ctrl_c() {
    signal sig;
    sig.add(SIGINT);
    sig.add(SIGTERM);
    co_return co_await sig.wait();
  }

  asio::signal_set &asio_handle() noexcept { return signals_; }

private:
  asio::signal_set signals_;
};

} // namespace coasio

#endif // !COASIO_SIGNAL_HPP