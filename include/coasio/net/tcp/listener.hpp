#ifndef COASIO_NET_TCP_LISTENER_HPP
#define COASIO_NET_TCP_LISTENER_HPP
#include "coasio/runtime.hpp"

#include <asio/ip/tcp.hpp>

#include "coasio/detail/async_op.hpp"
#include "coasio/detail/fwd.hpp"
#include "coasio/net/tcp/endpoint.hpp"
#include "coasio/net/tcp/socket.hpp"

namespace coasio::net::tcp {
class listener {
public:
  using wait_type = asio::ip::tcp::acceptor::wait_type;

  static listener create() {
    return listener{asio::ip::tcp::acceptor{runtime::get_current_io_context()}};
  }

  static std::expected<listener, std::error_code>
  bind(endpoint ep, const bool reuse_address = true) {
    auto listener_ = create();
    asio::error_code ec;

    listener_.asio_handle().open(ep.asio_endpoint().protocol(), ec);
    if (ec)
      return std::unexpected{ec};

    if (reuse_address) {
      listener_.asio_handle().set_option(
          asio::ip::tcp::acceptor::reuse_address(true), ec);
      if (ec)
        return std::unexpected{ec};
    }

    listener_.asio_handle().bind(ep.asio_endpoint(), ec);
    if (ec)
      return std::unexpected{ec};
    return listener_;
  }

  auto accept() {
    struct listen_awaiter {
      asio::error_code ec_;
      listener &listener_;
      std::optional<socket> socket_;
      std::shared_ptr<cancel_scope> scope_{};
      cancel_guard guard_;
      asio::cancellation_signal sig_{};

      explicit listen_awaiter(listener &listener)
          : listener_{listener}, socket_{std::nullopt} {}

      void set_cancel_scope(std::shared_ptr<cancel_scope> s) noexcept {
        guard_.bind(scope_ = std::move(s));
      }

      bool await_ready() noexcept {
        if (detail::already_cancelled(scope_)) {
          ec_ = asio::error::operation_aborted;
          return true;
        }
        return false;
      }

      void await_suspend(std::coroutine_handle<> h) noexcept {
        runtime *rt = detail::current_runtime();
        worker *w = detail::current_worker();
        auto r =
            guard_.arm([this] { sig_.emit(asio::cancellation_type::all); });
        if (r == cancel_guard::arm_result::already_cancelled) {
          ec_ = asio::error::operation_aborted;
          rt->schedule_in(w, h);
          return;
        }

        listener_.asio_handle().async_accept(asio::bind_cancellation_slot(
            sig_.slot(), [this, h, w, rt](const asio::error_code &ec,
                                          asio::ip::tcp::socket sock) {
              guard_.disarm();
              ec_ = ec;
              if (!ec_) {
                socket_ = socket{(std::move(sock))};
              }
              detail::runtime_schedule_in(rt, w, h);
            }));
      }

      std::expected<socket, std::error_code> await_resume() noexcept {
        guard_.disarm();
        if (ec_)
          return std::unexpected{ec_};
        return std::move(socket_.value());
      }
    };

    return listen_awaiter{*this};
  }

  std::expected<void, std::error_code>
  listen(const int backlog = asio::socket_base::max_listen_connections) {
    asio::error_code ec;
    acceptor_.listen(backlog, ec);
    if (ec)
      return std::unexpected{ec};
    return {};
  }

  auto wait(wait_type type) {
    return detail::async_op<void>([this, type]<typename Args>(Args &&token) {
      asio_handle().async_wait(type, std::forward<Args>(token));
    });
  }

  std::expected<void, std::error_code> close() {
    std::error_code ec_;
    acceptor_.close(ec_);
    if (ec_)
      return std::unexpected(std::move(ec_));
    return {};
  }

  bool is_open() const { return acceptor_.is_open(); }

  std::expected<endpoint, std::error_code> local_endpoint() const noexcept {
    asio::error_code ec;
    auto ep = acceptor_.local_endpoint(ec);
    if (ec)
      return std::unexpected{ec};
    return endpoint{ep};
  }

  auto native_handle() noexcept { return acceptor_.native_handle(); }

  asio::ip::tcp::acceptor &asio_handle() noexcept { return acceptor_; }

private:
  explicit listener(asio::ip::tcp::acceptor acceptor)
      : acceptor_(std::move(acceptor)) {}
  asio::ip::tcp::acceptor acceptor_;
};
} // namespace coasio::net::tcp

#endif // !COASIO_NET_TCP_LISTENER_HPP
