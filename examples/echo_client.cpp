#include <coasio.hpp>
#include <coasio/net/tcp.hpp>
#include <coasio/time.hpp>

#include <random>
#include <ranges>
#include <vector>

#define COASIO_MAIN$()                                                         \
  auto __user_main() -> ::coasio::task<int>;                                   \
  int main() {                                                                 \
    ::coasio::runtime rt;                                                      \
    rt.block_on(__user_main());                                                \
  }                                                                            \
  auto __user_main() -> ::coasio::task<int>

coasio::task<void> client(const size_t id, const size_t message_size) {
  auto maybeStream =
      co_await coasio::net::tcp::socket::connect_to("127.0.0.1:8080");
  if (!maybeStream) {
    std::cerr << "[Error] connect failed: " << maybeStream.error().message()
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

#include <coasio/filesystem.hpp>

COASIO_MAIN$() {
  auto maybeSize = co_await coasio::filesystem::file_size(
      R"(C:\dev\cpp\coasio\examples\echo_client.cpp)");
  if (!maybeSize) {
    std::cerr << "[Error] file_size failed: " << maybeSize.error().message()
              << std::endl;
    co_return 1;
  }
  std::cout << "file size: " << maybeSize.value() << std::endl;
  co_return 0;
}

coasio::task<int> bfdhjh() {
  constexpr size_t client_number = 20;
  constexpr size_t message_size = 1024;
  for (size_t i = 0; i < client_number; i++) {
    coasio::spawn(client(i, message_size));
  }

  while (true) {
    if (auto slp_res = co_await coasio::time::sleep(std::chrono::seconds(1));
        !slp_res) {
      std::cerr << "[Error] sleep failed: " << slp_res.error().message()
                << std::endl;
      break;
    }
  }
  co_return 0;
}
