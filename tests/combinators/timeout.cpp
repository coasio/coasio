#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <coasio.hpp>
#include <coasio/combinators/timeout.hpp>
#include <coasio/sync/oneshot.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;
using namespace coasio::combinators;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

TEST_CASE("timeout: operation completes before deadline",
          "[combinators][timeout]") {
  sync_run([]() -> coasio::task<void> {
    auto fast = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };

    auto res = co_await timeout(500ms, fast());
    REQUIRE(res.has_value());
    REQUIRE(*res == 42);
    co_return;
  }());
}

TEST_CASE("timeout: deadline expires before operation completes",
          "[combinators][timeout]") {
  std::atomic<bool> operation_cancelled{false};

  sync_run([](std::atomic<bool> *cancelled) -> coasio::task<void> {
    auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        flag->store(true, std::memory_order_release);
      }
      co_return 99;
    };

    auto res = co_await timeout(20ms, slow(cancelled));
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == std::make_error_code(std::errc::timed_out));
    REQUIRE(cancelled->load(std::memory_order_acquire));
    co_return;
  }(&operation_cancelled));
}

TEST_CASE("timeout: supports alternative argument order timeout(op, duration)",
          "[combinators][timeout]") {
  sync_run([]() -> coasio::task<void> {
    auto fast = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 100;
    };

    auto res = co_await timeout(fast(), 500ms);
    REQUIRE(res.has_value());
    REQUIRE(*res == 100);
    co_return;
  }());
}

TEST_CASE("timeout: handles void return type", "[combinators][timeout]") {
  SECTION("void completes before deadline") {
    sync_run([]() -> coasio::task<void> {
      auto fast_void = []() -> coasio::task<void> {
        co_await coasio::time::sleep(10ms);
        co_return;
      };

      auto res = co_await timeout(500ms, fast_void());
      REQUIRE(res.has_value());
      co_return;
    }());
  }

  SECTION("void times out") {
    std::atomic<bool> void_cancelled{false};
    sync_run([](std::atomic<bool> *cancelled) -> coasio::task<void> {
      auto slow_void = [](std::atomic<bool> *flag) -> coasio::task<void> {
        auto r = co_await coasio::time::sleep(5s);
        if (!r.has_value()) {
          flag->store(true, std::memory_order_release);
        }
        co_return;
      };

      auto res = co_await timeout(20ms, slow_void(cancelled));
      REQUIRE_FALSE(res.has_value());
      REQUIRE(res.error() == std::make_error_code(std::errc::timed_out));
      REQUIRE(cancelled->load(std::memory_order_acquire));
      co_return;
    }(&void_cancelled));
  }
}

TEST_CASE("timeout: unwraps nested std::expected<T, std::error_code>",
          "[combinators][timeout]") {
  SECTION("inner success is unwrapped") {
    sync_run([]() -> coasio::task<void> {
      auto op_success =
          []() -> coasio::task<std::expected<std::string, std::error_code>> {
        co_await coasio::time::sleep(10ms);
        co_return "hello coasio";
      };

      auto res = co_await timeout(500ms, op_success());
      REQUIRE(res.has_value());
      REQUIRE(*res == "hello coasio");
      co_return;
    }());
  }

  SECTION("inner error is preserved and unwrapped") {
    sync_run([]() -> coasio::task<void> {
      auto op_error =
          []() -> coasio::task<std::expected<std::string, std::error_code>> {
        co_await coasio::time::sleep(10ms);
        co_return std::unexpected(coasio::error::channel_closed);
      };

      auto res = co_await timeout(500ms, op_error());
      REQUIRE_FALSE(res.has_value());
      REQUIRE(res.error() == coasio::error::channel_closed);
      co_return;
    }());
  }
}

TEST_CASE("timeout: distinguishes timeout from parent cancellation",
          "[combinators][timeout]") {
  coasio::runtime rt{};

  std::atomic<bool> task_cancelled{false};

  auto h = rt.spawn([](std::atomic<bool> *cancelled) -> coasio::task<void> {
    auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        flag->store(true, std::memory_order_release);
      }
      co_return 1;
    };

    auto res = co_await timeout(5s, slow(cancelled));
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == coasio::error::cancelled);
    co_return;
  }(&task_cancelled));

  std::this_thread::sleep_for(20ms);
  h.abort();
  auto join_res = rt.block_on(h.join());

  REQUIRE(join_res.has_value());
  REQUIRE(task_cancelled.load(std::memory_order_acquire));
}

TEST_CASE("timeout: pre-cancelled parent returns error::cancelled",
          "[combinators][timeout]") {
  coasio::runtime rt{};

  std::atomic<bool> task_cancelled{false};

  auto h = rt.spawn([](std::atomic<bool> *cancelled) -> coasio::task<void> {
    co_await coasio::time::sleep(50ms);

    auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        flag->store(true, std::memory_order_release);
      }
      co_return 1;
    };

    auto res = co_await timeout(5s, slow(cancelled));
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == coasio::error::cancelled);
    co_return;
  }(&task_cancelled));

  h.abort();
  auto join_res = rt.block_on(h.join());

  REQUIRE(join_res.has_value());
  REQUIRE(task_cancelled.load(std::memory_order_acquire));
}

TEST_CASE("timeout: exception in operation drains timer and propagates",
          "[combinators][timeout]") {
  REQUIRE_THROWS_AS(sync_run([]() -> coasio::task<void> {
                      auto failing = []() -> coasio::task<int> {
                        co_await coasio::time::sleep(10ms);
                        throw std::runtime_error("timeout branch exception");
                        co_return 0;
                      };

                      co_await timeout(5s, failing());
                      co_return;
                    }()),
                    std::runtime_error);
}

TEST_CASE("timeout: waits for loser cleanup before returning on expiry",
          "[combinators][timeout]") {
  std::atomic<bool> loser_cleaned_up{false};

  sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
    auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        co_await coasio::time::sleep(0ms);
        flag->store(true, std::memory_order_release);
      }
      co_return 0;
    };

    auto res = co_await timeout(10ms, slow(cleaned));
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == std::make_error_code(std::errc::timed_out));
    REQUIRE(cleaned->load(std::memory_order_acquire));
    co_return;
  }(&loser_cleaned_up));
}

TEST_CASE("timeout: non-cooperative timed-out task blocks combinator return",
          "[combinators][timeout]") {
  coasio::runtime rt{};

  auto [gate_tx, gate_rx] = coasio::sync::make_oneshot<int>();

  std::atomic<bool> timeout_returned{false};
  std::atomic<bool> op_finished{false};

  auto gate_tx_ptr =
      std::make_shared<coasio::sync::oneshot::sender<int>>(std::move(gate_tx));

  auto h = rt.spawn([](coasio::sync::oneshot::receiver<int> rx,
                       std::atomic<bool> *returned,
                       std::atomic<bool> *finished) -> coasio::task<void> {
    auto blocking = [](coasio::sync::oneshot::receiver<int> gate,
                       std::atomic<bool> *op_fin) -> coasio::task<int> {
      auto val = co_await coasio::detail::uncancellable(std::move(gate));
      op_fin->store(true, std::memory_order_release);
      if (val.has_value())
        co_return *val;
      co_return -1;
    };

    auto res = co_await timeout(10ms, blocking(std::move(rx), finished));
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == std::make_error_code(std::errc::timed_out));
    returned->store(true, std::memory_order_release);
    co_return;
  }(std::move(gate_rx), &timeout_returned, &op_finished));

  std::this_thread::sleep_for(50ms);
  REQUIRE_FALSE(timeout_returned.load(std::memory_order_acquire));
  REQUIRE_FALSE(op_finished.load(std::memory_order_acquire));

  auto send_res = gate_tx_ptr->send(1234);
  REQUIRE(send_res.has_value());

  auto join_res = rt.block_on(h.join());
  REQUIRE(join_res.has_value());

  REQUIRE(timeout_returned.load(std::memory_order_acquire));
  REQUIRE(op_finished.load(std::memory_order_acquire));
}
