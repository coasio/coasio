#ifndef COASIO_NET_BASE_RESOLVER_HPP
#define COASIO_NET_BASE_RESOLVER_HPP

#include <string>
#include <string_view>

#include <asio/ip/basic_resolver.hpp>

#include "coasio/detail/async_op.hpp"
#include "coasio/runtime.hpp"

namespace coasio::net {
template <typename InternetProtocol> class base_resolver {
public:
  typedef InternetProtocol protocol_type;
  using results_type = asio::ip::basic_resolver<protocol_type>::results_type;

  static base_resolver create() noexcept {
    return base_resolver{detail::current_runtime()->get_current_io_context()};
  }

  auto resolve(std::string_view host, std::string_view service) {
    return detail::async_op<results_type>(
        [this, host = std::string(host),
         service = std::string(service)]<typename Args>(Args &&token) {
          asio_handle().async_resolve(host, service, std::forward<Args>(token));
        });
  }

  auto &asio_handle() noexcept { return resolver_; }

private:
  explicit base_resolver(asio::io_context &io_context) noexcept
      : resolver_{io_context} {}

  asio::ip::basic_resolver<protocol_type> resolver_;
};
}; // namespace coasio::net

#endif // !COASIO_NET_BASE_RESOLVER_HPP
