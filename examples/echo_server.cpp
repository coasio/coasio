#include <coasio.hpp>

#include "coasio/net/tcp.hpp"
#include "coasio/time.hpp"

#include <atomic>
#include <cmath>

#define COASIO_MAIN$()                                                         \
  auto __user_main() -> ::coasio::task<int>;                                   \
  int main() {                                                                 \
    ::coasio::runtime rt;                                                      \
    rt.block_on(__user_main());                                                \
  }                                                                            \
  auto __user_main() -> ::coasio::task<int>

struct ServerStats {
  std::atomic<size_t> active_clients{0};
  std::atomic<uint64_t> total_bytes_received{0};
  std::atomic<uint64_t> total_bytes_sent{0};
};

inline ServerStats g_stats;

class RollingMetrics {
public:
  explicit RollingMetrics(size_t window_size = 60) : max_size(window_size) {
    samples.reserve(window_size);
  }

  void add_sample(double val) {
    if (samples.size() < max_size) {
      samples.push_back(val);
    } else {
      samples[head] = val;
      head = (head + 1) % max_size;
    }
  }

  std::tuple<double, double, double> get_percentiles() const {
    if (samples.empty())
      return {0.0, 0.0, 0.0};

    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    return {get_percentile_value(sorted, 0.50),
            get_percentile_value(sorted, 0.90),
            get_percentile_value(sorted, 0.99)};
  }

private:
  double get_percentile_value(const std::vector<double> &sorted,
                              double percentile) const {
    size_t idx = static_cast<size_t>(std::ceil(percentile * sorted.size())) - 1;
    return sorted[std::clamp(idx, size_t(0), sorted.size() - 1)];
  }

  std::vector<double> samples;
  size_t max_size;
  size_t head = 0;
};

coasio::task<void> metrics_monitor_task() {
  uint64_t last_received = 0;
  uint64_t last_sent = 0;

  RollingMetrics in_metrics(60);
  RollingMetrics out_metrics(60);

  for (;;) {
    auto slp_res = co_await coasio::time::sleep(std::chrono::seconds(1));
    if (!slp_res) {
      std::cerr << "[Error] sleep failed: " << slp_res.error().message()
                << std::endl;
      co_return;
    }

    uint64_t current_received = g_stats.total_bytes_received.load();
    uint64_t current_sent = g_stats.total_bytes_sent.load();
    size_t current_clients = g_stats.active_clients.load();

    uint64_t bytes_in_per_sec = current_received - last_received;
    uint64_t bytes_out_per_sec = current_sent - last_sent;

    last_received = current_received;
    last_sent = current_sent;

    double download_speed = bytes_in_per_sec / 1024.0;
    double upload_speed = bytes_out_per_sec / 1024.0;

    in_metrics.add_sample(download_speed);
    out_metrics.add_sample(upload_speed);

    auto [in_p50, in_p90, in_p99] = in_metrics.get_percentiles();
    auto [out_p50, out_p90, out_p99] = out_metrics.get_percentiles();

    std::cout
        << "\033[H\033[J" // Clear screen ANSI escape code for clean viewing
        << "================ SERVER MONITOR ================\n"
        << "Active Clients: " << current_clients << "\n"
        << "Current Speed:  In: " << std::fixed << std::setprecision(2)
        << download_speed << " KB/s | Out: " << upload_speed << " KB/s\n"
        << "------------------------------------------------\n"
        << "SPEED IN  (Percentiles over 60s):\n"
        << "  p50: " << in_p50 << " KB/s | p90: " << in_p90
        << " KB/s | p99: " << in_p99 << " KB/s\n"
        << "SPEED OUT (Percentiles over 60s):\n"
        << "  p50: " << out_p50 << " KB/s | p90: " << out_p90
        << " KB/s | p99: " << out_p99 << " KB/s\n"
        << "================================================\n"
        << std::flush;
  }
}

coasio::task<void> client_handler(coasio::net::tcp::socket sk) {
  ++g_stats.active_clients;

  struct ClientGuard {
    ~ClientGuard() { --g_stats.active_clients; }
  } guard;

  std::vector<std::byte> buffer(1024);
  while (true) {
    auto maybeRead = co_await sk.read_some(buffer);
    if (!maybeRead) {
      std::cerr << "[Error] read " << maybeRead.error().message() << std::endl;
      break;
    }
    const auto bytesRead = *maybeRead;
    g_stats.total_bytes_received += bytesRead;
    if (bytesRead == 0) {
      break;
    }

    auto write_res = co_await sk.write({buffer.data(), bytesRead});
    if (!write_res) {
      std::cerr << "[Error] write " << write_res.error().message() << std::endl;
      break;
    }

    const size_t bytes_written = *write_res;
    g_stats.total_bytes_sent += bytes_written;
  }
  co_return;
}

COASIO_MAIN$() {
  auto maybeListener = coasio::net::tcp::listener::bind(
      coasio::net::tcp::endpoint{coasio::net::tcp::ip_address::v4(), 8080});
  if (!maybeListener) {
    std::cerr << "[Error] listener " << maybeListener.error().message()
              << std::endl;
    co_return 1;
  }
  auto listener = std::move(*maybeListener);

  if (auto res = listener.listen(); !res) {
    std::cerr << "[Error] listen failed: " << res.error().message()
              << std::endl;
    co_return -1;
  }

  coasio::spawn(metrics_monitor_task());

  for (;;) {
    auto maybeSocket = co_await listener.accept();
    if (!maybeSocket) {
      std::cerr << "[Error] accept " << maybeSocket.error().message()
                << std::endl;
      co_return 1;
    }
    auto socket = std::move(*maybeSocket);
    coasio::spawn(client_handler(std::move(socket)));
  }

  co_return 0;
}
