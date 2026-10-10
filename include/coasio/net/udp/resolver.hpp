#ifndef COASIO_NET_UDP_RESOLVER_HPP
#define COASIO_NET_UDP_RESOLVER_HPP
#include <asio/ip/udp.hpp>

#include "coasio/net/base_resolver.hpp"

namespace coasio::net::udp {
using resolver = base_resolver<asio::ip::udp>;
} // namespace coasio::net::udp

#endif // !COASIO_NET_UDP_RESOLVER_HPP
