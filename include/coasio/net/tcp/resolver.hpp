#ifndef COASIO_NET_TCP_RESOLVER_HPP
#define COASIO_NET_TCP_RESOLVER_HPP
#include <asio/ip/tcp.hpp>

#include "coasio/net/base_resolver.hpp"

namespace coasio::net::tcp {
using resolver = base_resolver<asio::ip::tcp>;
} // namespace coasio::net::tcp

#endif // !COASIO_NET_TCP_RESOLVER_HPP
