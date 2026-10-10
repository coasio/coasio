#ifndef COASIO_NET_UDP_ENDPOINT_HPP
#define COASIO_NET_UDP_ENDPOINT_HPP
#include <asio/ip/udp.hpp>

#include "coasio/net/base_endpoint.hpp"

namespace coasio::net::udp {
using endpoint = base_endpoint<asio::ip::udp>;
} // namespace coasio::net::udp

#endif // !COASIO_NET_UDP_ENDPOINT_HPP
