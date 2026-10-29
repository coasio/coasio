#include <coasio.hpp>
#include <coasio/net/tcp.hpp>
#include <coasio/signal.hpp>

#include <print>
#include <random>
#include <vector>

#define COASIO_MAIN$()                                                         \
  auto __user_main() -> ::coasio::task<int>;                                   \
  int main() {                                                                 \
    ::coasio::runtime rt;                                                      \
    rt.block_on(__user_main());                                                \
  }                                                                            \
  auto __user_main() -> ::coasio::task<int>

coasio::task<void> client(const size_t id, const size_t message_size) {
  auto ts = [] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  std::println("[{} ms] [client {}] coroutine entered", ts(), id);
  auto t0 = ts();
  auto maybeStream =
      co_await coasio::net::tcp::socket::connect_to(coasio::net::tcp::endpoint{
          coasio::net::ip_address::from_string("127.0.0.1").value(), 8080});
  std::println("[{} ms] [client {}] connect returned in {} ms", ts(), id,
               ts() - t0);
  if (!maybeStream) {
    std::cerr << "[Error] [" << id
              << "] connect failed: " << maybeStream.error().message()
              << std::endl;
    co_return;
  }
  auto stream = std::move(*maybeStream);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<unsigned short> distrib(0, 255);

  std::vector<std::byte> buffer(message_size);
  std::ranges::generate(buffer,
                        [&]() { return static_cast<std::byte>(distrib(gen)); });
  while (true) {
    if (auto wr_res = co_await stream.write(buffer); !wr_res) {
      std::cerr << "[Error] [client " << id
                << "] write failed: " << wr_res.error().message() << std::endl;
      break;
    }

    auto rd_res = co_await stream.read_some(buffer);
    if (!rd_res) {
      std::cerr << "[Error] [client " << id
                << "] read failed: " << rd_res.error().message() << std::endl;
      break;
    }
    if (*rd_res == 0) {
      std::cerr << "[Error] [client " << id << "] connection closed"
                << std::endl;
      break;
    }
  }
}

COASIO_MAIN$() {
  constexpr size_t client_number = 1000;
  for (size_t i = 0; i < client_number; i++) {
    constexpr size_t message_size = 1024;
    coasio::spawn(client(i, message_size));
  }

  auto _ = co_await coasio::signal::ctrl_c();

  co_return 0;
}