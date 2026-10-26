#ifndef COASIO_NET_HELPER_HPP
#define COASIO_NET_HELPER_HPP

#include <algorithm>
#include <charconv>
#include <expected>
#include <optional>
#include <system_error>

namespace coasio::net {
inline std::string_view trim_whitespace(const std::string_view sv) noexcept {
  size_t start = 0;
  size_t end = sv.length();
  bool s = true;
  for (size_t i = 0; i < sv.length(); ++i) {
    auto c = static_cast<unsigned char>(sv[i]);
    if (s) {
      start = i;
      if (!std::isspace(c)) {
        s = false;
      }
    } else {
      end = i;
      if (std::isspace(c)) {
        break;
      }
    }
  }

  return (start < end)
             ? std::string_view{sv.data() + start, sv.data() + end + 1}
             : std::string_view{};
}

inline std::expected<std::uint16_t, std::error_code>
parse_port(const std::string_view port_str) noexcept {
  if (port_str.empty()) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  for (char c : port_str) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
  }

  unsigned long value = 0;
  auto [ptr, ec] = std::from_chars(port_str.data(),
                                   port_str.data() + port_str.size(), value);
  if (ec != std::errc{}) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  if (value > 65535) {
    return std::unexpected(
        std::make_error_code(std::errc::result_out_of_range));
  }
  return static_cast<std::uint16_t>(value);
}

struct host_port {
  std::string_view host;
  std::optional<uint16_t> port;
};

inline std::expected<host_port, std::error_code>
parse_host_port(std::string_view input) noexcept(false) {
  auto trimmed = trim_whitespace(input);
  if (trimmed.empty()) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  if (trimmed.front() == '[') {
    const auto close_bracket = trimmed.find(']');
    if (close_bracket == std::string_view::npos) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const auto host = trimmed.substr(1, close_bracket - 1);
    if (host.empty()) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    std::optional<std::uint16_t> port = std::nullopt;
    if (close_bracket + 1 < trimmed.size()) {
      if (trimmed[close_bracket + 1] != ':') {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
      }
      const auto port_str = trimmed.substr(close_bracket + 2);
      auto parsed_port = parse_port(port_str);
      if (!parsed_port) {
        return std::unexpected(parsed_port.error());
      }
      port = *parsed_port;
    }

    return host_port{host, port};
  }

  const auto colon_count = std::count(trimmed.begin(), trimmed.end(), ':');

  if (colon_count == 0) {
    return host_port{trimmed, std::nullopt};
  }

  if (colon_count == 1) {
    const auto colon_pos = trimmed.find(':');
    const auto host = trimmed.substr(0, colon_pos);
    const auto port_str = trimmed.substr(colon_pos + 1);

    if (host.empty() || port_str.empty()) {
      return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    auto parsed_port = parse_port(port_str);
    if (!parsed_port) {
      return std::unexpected(parsed_port.error());
    }
    return host_port{host, *parsed_port};
  }

  return host_port{trimmed, std::nullopt};
}
}; // namespace coasio::net

#endif // !COASIO_NET_HELPER_HPP
