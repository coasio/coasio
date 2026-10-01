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

  auto blocker_running = std::make_shared<std::atomic<bool>>(false);
  auto blocker_release = std::make_shared<std::atomic<bool>>(false);

  auto blocker = rt.spawn(
      [](auto br, auto brel, coasio::runtime &r) -> coasio::task<void> {
        auto res = co_await r.spawn_blocking([br, brel] {
          br->store(true, std::memory_order_release);
          while (!brel->load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
        co_return;
      }(blocker_running, blocker_release, rt));

  while (!blocker_running->load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto queued_task_executed = std::make_shared<std::atomic<bool>>(false);
  auto victim =
      rt.spawn([](auto qt_exec, coasio::runtime &r) -> coasio::task<void> {
        auto res = co_await r.spawn_blocking(
            [qt_exec] { qt_exec->store(true, std::memory_order_relaxed); });
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error() == coasio::error::cancelled);
      }(queued_task_executed, rt));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  victim.abort();
  rt.block_on(victim.join());

  REQUIRE_FALSE(queued_task_executed->load(std::memory_order_relaxed));

  blocker_release->store(true, std::memory_order_release);
  rt.block_on(blocker.join());
}
