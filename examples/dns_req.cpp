#include <iomanip>
#include <iostream>

#include "coasio.hpp"
#include "coasio/net/udp.hpp"
#include "coasio/time.hpp"

constexpr std::array<std::byte, 30> dns_query = {
    std::byte{0xaa}, std::byte{0xbb}, // Transaction ID
    std::byte{0x01}, std::byte{0x00}, // Flags: Standard query
    std::byte{0x00}, std::byte{0x01}, // Questions: 1
    std::byte{0x00}, std::byte{0x00}, // Answer RRs: 0
    std::byte{0x00}, std::byte{0x00}, // Authority RRs: 0
    std::byte{0x00}, std::byte{0x00}, // Additional RRs: 0

    // Query name: "example.com"
    std::byte{0x07}, std::byte{'e'}, std::byte{'x'}, std::byte{'a'},
    std::byte{'m'}, std::byte{'p'}, std::byte{'l'}, std::byte{'e'},
    std::byte{0x03}, std::byte{'c'}, std::byte{'o'}, std::byte{'m'},
    std::byte{0x00},

    std::byte{0x00}, std::byte{0x01}, // Type: A (IPv4 address)
    std::byte{0x00}, std::byte{0x01}  // Class: IN (Internet)
};

coasio::task<void> req() {
  auto socket = coasio::net::udp::socket::create();
  coasio::net::udp::endpoint server_endpoint{
      coasio::net::ip_address::from_string("8.8.8.8").value(), 53};
  const auto r =
      socket.bind(coasio::net::udp::endpoint{coasio::net::ip_address::v4(), 0});
  if (!r) {
    std::cerr << "[Error] bind failed: " << r.error().message() << std::endl;
    co_return;
  }

  auto size = co_await socket.send_to(dns_query, server_endpoint);
  if (!size) {
    std::cerr << "[Error] send failed: " << size.error().message() << std::endl;
    co_return;
  }

  coasio::net::udp::endpoint server{};
  std::array<std::byte, 1024> buffer;
  size = co_await socket.receive_from(buffer, server);
  if (!size) {
    std::cerr << "[Error] receive failed: " << size.error().message()
              << std::endl;
    co_return;
  }

  std::cout << "server: " << server.address().to_string() << ":"
            << server.port() << std::endl;
  std::cout << "received: " << size.value() << " bytes" << std::endl;

  std::cout << "Hex: ";
  for (size_t i = 0; i < size.value(); ++i) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(buffer[i]));
  }
  std::cout << std::dec << std::endl;

  co_return;
}

int main() {
  coasio::runtime rt{};
  rt.block_on([] -> coasio::task<void> {
    auto t = coasio::spawn(req);
    // TODO change to a coasio::timeout when available
    auto _ = co_await coasio::time::sleep(std::chrono::seconds(3));
    t.abort();
    t.join();
    co_return;
  });
  return 0;
}