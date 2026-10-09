#ifndef COASIO_NET_TCP_SOCKET_HPP
#define COASIO_NET_TCP_SOCKET_HPP

#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include "coasio/detail/async_op.hpp"
#include "coasio/detail/byte_like.hpp"
#include "coasio/net/helper.hpp"
#include "endpoint.hpp"
#include "resolver.hpp"

namespace coasio::net::tcp {
class socket {
  auto connect_impl(const asio::ip::tcp::resolver::results_type &endpoints) {
    return detail::async_op<void>(
        [this, endpoints]<typename Args>(Args &&token) {
          asio::async_connect(asio_handle(), endpoints.begin(), endpoints.end(),
                              std::forward<Args>(token));
        });
  }

public:
  using wait_type = asio::ip::tcp::socket::wait_type;
  using shutdown_type = asio::ip::tcp::socket::shutdown_type;

  explicit socket(asio::ip::tcp::socket socket) noexcept
      : socket_{std::move(socket)} {}

  static socket create() noexcept {
    return socket{asio::ip::tcp::socket{runtime::get_current_io_context()}};
  }

  static task<std::expected<socket, std::error_code>>
  connect_to(std::string_view host_and_port) {
    auto maybe_hap = parse_host_port(host_and_port);
    if (!maybe_hap) {
      co_return std::unexpected(maybe_hap.error());
    }
    if (!maybe_hap.value().port.has_value()) {
      co_return std::unexpected(
          std::make_error_code(std::errc::invalid_argument));
    }
    auto [host, port] = maybe_hap.value();

    resolver r = resolver::create();
    auto maybe_eps = co_await r.resolve(host, std::to_string(port.value()));
    if (!maybe_eps) {
      co_return std::unexpected(maybe_eps.error());
    }

    auto sck = socket::create();
    auto result = co_await sck.connect_impl(*maybe_eps);
    if (!result) {
      co_return std::unexpected(result.error());
    }
    co_return std::move(sck);
  }

  auto connect(const endpoint &ep) {
    return detail::async_op<void>([this, ep]<typename Args>(Args &&token) {
      asio_handle().async_connect(ep.asio_endpoint(),
                                  std::forward<Args>(token));
    });
  }

  std::expected<void, std::error_code> bind(const endpoint &ep) {
    std::error_code ec_;
    if (!socket_.is_open()) {
      socket_.open(ep.asio_endpoint().protocol(), ec_);
      if (ec_)
        return std::unexpected(ec_);
    }
    socket_.bind(ep.asio_endpoint(), ec_);
    if (ec_)
      return std::unexpected(ec_);
    return {};
  }

  template <std::ranges::contiguous_range R>
    requires coasio::detail::byte_like<std::ranges::range_reference_t<R>> &&
             (!std::is_const_v<
                 std::remove_reference_t<std::ranges::range_reference_t<R>>>)
  auto read(R &&buffer) {
    return read(std::as_writable_bytes(
        std::span{std::ranges::data(buffer), std::ranges::size(buffer)}));
  }

  auto read(std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio::async_read(asio_handle(), asio::buffer(buffer),
                           std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires coasio::detail::byte_like<std::ranges::range_reference_t<R>> &&
             (!std::is_const_v<
                 std::remove_reference_t<std::ranges::range_reference_t<R>>>)
  auto read_some(R &&buffer) {
    return read_some(std::as_writable_bytes(
        std::span{std::ranges::data(buffer), std::ranges::size(buffer)}));
  }

  auto read_some(std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio_handle().async_read_some(asio::buffer(buffer),
                                        std::forward<Args>(token));
        });
  }

  // TODO: read_until

  template <std::ranges::contiguous_range R>
    requires coasio::detail::byte_like<std::ranges::range_reference_t<R>>
  auto write(const R &buffer) {
    return write(std::as_bytes(
        std::span{std::ranges::data(buffer), std::ranges::size(buffer)}));
  }

  auto write(std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio::async_write(asio_handle(), asio::buffer(buffer),
                            std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires coasio::detail::byte_like<std::ranges::range_reference_t<R>>
  auto write_some(const R &buffer) {
    return write_some(std::as_bytes(
        std::span{std::ranges::data(buffer), std::ranges::size(buffer)}));
  }

  auto write_some(std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio_handle().async_write_some(asio::buffer(buffer),
                                         std::forward<Args>(token));
        });
  }

  auto wait(wait_type type) {
    return detail::async_op<void>([this, type]<typename Args>(Args &&token) {
      asio_handle().async_wait(type, std::forward<Args>(token));
    });
  }

  std::expected<size_t, std::error_code> available() const {
    std::error_code ec_;
    size_t bytes_available = socket_.available(ec_);
    if (ec_)
      return std::unexpected(ec_);
    return bytes_available;
  }

  std::expected<void, std::error_code> shutdown(const shutdown_type type) {
    std::error_code ec_;
    socket_.shutdown(type, ec_);
    if (ec_)
      return std::unexpected(ec_);
    return {};
  }

  std::expected<void, std::error_code> close() {
    std::error_code ec_;
    socket_.close(ec_);
    if (ec_)
      return std::unexpected(ec_);
    return {};
  }

  [[nodiscard]] bool is_open() const { return socket_.is_open(); }

  std::expected<endpoint, std::error_code> remote_endpoint() const noexcept {
    asio::error_code ec;
    auto ep = socket_.remote_endpoint(ec);
    if (ec)
      return std::unexpected(ec);
    return endpoint{ep};
  }

  std::expected<endpoint, std::error_code> local_endpoint() const noexcept {
    asio::error_code ec;
    auto ep = socket_.local_endpoint(ec);
    if (ec)
      return std::unexpected(ec);
    return endpoint{ep};
  }

  auto native_handle() noexcept { return socket_.native_handle(); }

  asio::ip::tcp::socket &asio_handle() noexcept { return socket_; }

private:
  asio::ip::tcp::socket socket_;
};
}; // namespace coasio::net::tcp

#endif // !COASIO_NET_TCP_SOCKET_HPP
