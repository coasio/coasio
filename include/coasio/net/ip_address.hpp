#ifndef COASIO_NET_IP_ADDRESS_HPP
#define COASIO_NET_IP_ADDRESS_HPP

#include <expected>

#include <asio/ip/address.hpp>
#include <utility>

namespace coasio::net {
class ip_address {
public:
  ip_address() = default;

  explicit ip_address(asio::ip::address address) noexcept
      : address_{std::move(address)} {}

  static ip_address v4() noexcept {
    return ip_address{asio::ip::address_v4::any()};
  }

  static ip_address v6() noexcept {
    return ip_address{asio::ip::address_v6::any()};
  }

  static ip_address v4(const uint32_t addr) {
    return ip_address{asio::ip::address_v4{addr}};
  }

  static ip_address v6(const std::array<uint8_t, 16> &addr) {
    return ip_address{asio::ip::address_v6{addr}};
  }

  static std::expected<ip_address, std::error_code>
  from_string(const std::string_view ip_string) noexcept {
    asio::error_code ec;
    const auto addr = asio::ip::make_address(ip_string, ec);
    if (ec) {
      return std::unexpected(ec);
    }
    return ip_address{addr};
  }

  [[nodiscard]] bool is_v4() const noexcept { return address_.is_v4(); }

  [[nodiscard]] bool is_v6() const noexcept { return address_.is_v6(); }

  [[nodiscard]] bool is_loopback() const noexcept {
    return address_.is_loopback();
  }

  [[nodiscard]] bool is_multicast() const noexcept {
    return address_.is_multicast();
  }

  [[nodiscard]] auto is_unspecified() const noexcept {
    return address_.is_unspecified();
  }

  [[nodiscard]] std::string to_string() const { return address_.to_string(); }

  [[nodiscard]] ip_address to_v4() const {
    return ip_address{address_.to_v4()};
  }

  [[nodiscard]] ip_address to_v6() const {
    return ip_address{address_.to_v6()};
  }

  [[nodiscard]] auto &asio_address() noexcept { return address_; }

  [[nodiscard]] const auto &asio_address() const noexcept { return address_; }

  std::ostream &operator<<(std::ostream &os) const { return os << address_; }

private:
  asio::ip::address address_{};
};
}; // namespace coasio::net

#endif // !COASIO_NET_IP_ADDRESS_HPP
