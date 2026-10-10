#ifndef COASIO_NET_BASE_ENDPOINT_HPP
#define COASIO_NET_BASE_ENDPOINT_HPP

#include <ostream>

#include <asio/ip/basic_endpoint.hpp>

#include "ip_address.hpp"

namespace coasio::net {
template <typename InternetProtocol> class base_endpoint {
public:
  typedef InternetProtocol protocol_type;

  base_endpoint() noexcept = default;

  explicit base_endpoint(
      asio::ip::basic_endpoint<protocol_type> endpoint) noexcept
      : endpoint_{endpoint} {}

  base_endpoint(ip_address addr, const uint16_t port) noexcept
      : endpoint_{addr.asio_address(), port} {}

  [[nodiscard]] ip_address address() const noexcept {
    return ip_address{endpoint_.address()};
  }

  [[nodiscard]] uint16_t port() const noexcept { return endpoint_.port(); }

  void set_address(ip_address &addr) noexcept {
    endpoint_.set_address(addr.asio_address());
  }

  void set_port(uint16_t port) noexcept { endpoint_.set_port(port); }

  friend std::ostream &operator<<(std::ostream &os, const base_endpoint &ep) {
    return os << ep.endpoint_;
  }

  [[nodiscard]] auto &asio_endpoint() noexcept { return endpoint_; }

  [[nodiscard]] const auto &asio_endpoint() const noexcept { return endpoint_; }

private:
  asio::ip::basic_endpoint<protocol_type> endpoint_;
};
}; // namespace coasio::net

#endif // !COASIO_NET_BASE_ENDPOINT_HPP
