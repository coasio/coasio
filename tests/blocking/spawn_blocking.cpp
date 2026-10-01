#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <coasio.hpp>

TEST_CASE("spawn_blocking runs on a blocking pool thread",
          "[runtime][blocking]") {
  coasio::runtime rt{};

  auto main_id = std::this_thread::get_id();

  auto r = rt.block_on([&]() -> coasio::task<std::thread::id> {
    auto res = co_await coasio::spawn_blocking(
        [] { return std::this_thread::get_id(); });
    co_return res.value();
  }());

  REQUIRE(r != std::thread::id{});
  REQUIRE(r != main_id);
}

TEST_CASE("spawn_blocking handles void return", "[runtime][blocking]") {
  coasio::runtime rt{};

  std::atomic<bool> executed{false};

  rt.block_on([&]() -> coasio::task<void> {
    auto res = co_await coasio::spawn_blocking([&] { executed = true; });
    REQUIRE(res.has_value());
    co_return;
  }());

  REQUIRE(executed.load());
}

TEST_CASE("spawn_blocking propagates exceptions", "[runtime][blocking]") {
  coasio::runtime rt{};

  rt.block_on([&]() -> coasio::task<void> {
    bool caught = false;
    try {
      auto r = co_await coasio::spawn_blocking(
          [] { throw std::runtime_error("test error"); });
    } catch (const std::runtime_error &e) {
      caught = true;
    }
    REQUIRE(caught);
    co_return;
  }());
}

TEST_CASE("spawn_blocking cancels while queued in pool",
          "[runtime][blocking][cancel]") {
  // TODO: When the runtime can be configured set one with a 1 thread blocking
  // pool for deterministic queueing
  coasio::runtime rt{};

  std::atomic<bool> blocker_running{false};
  std::atomic<bool> blocker_release{false};

  auto blocker = rt.spawn([&]() -> coasio::task<void> {
    auto res = co_await coasio::spawn_blocking([&] {
      blocker_running = true;
      while (!blocker_release.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
    co_return;
  });

  while (!blocker_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::atomic<bool> queued_task_executed{false};
  auto victim = rt.spawn([&]() -> coasio::task<void> {
    auto res = co_await rt.spawn_blocking([&] { queued_task_executed = true; });
    REQUIRE_FALSE(res.has_value());
    REQUIRE(res.error() == coasio::error::cancelled);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  victim.abort();
  rt.block_on(victim.join());

  REQUIRE_FALSE(queued_task_executed.load());

  blocker_release = true;
  rt.block_on(blocker.join());
}
