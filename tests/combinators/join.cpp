#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>

#include <coasio.hpp>
#include <coasio/combinators/join.hpp>
#include <coasio/sync/oneshot.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;
using namespace coasio::combinators;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

TEST_CASE("join: all branches complete and return tuple of results",
          "[combinators][join]") {
  sync_run([]() -> coasio::task<void> {
    auto task_a = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };

    auto task_b = []() -> coasio::task<std::string> {
      co_await coasio::time::sleep(20ms);
      co_return "hello";
    };

    auto res = co_await join(task_a(), task_b());
    REQUIRE(res.has_value());
    REQUIRE(std::get<0>(*res) == 42);
    REQUIRE(std::get<1>(*res) == "hello");
    co_return;
  }());
}

TEST_CASE("join: handles void tasks normalized to monostate",
          "[combinators][join]") {
  sync_run([]() -> coasio::task<void> {
    auto void_task = []() -> coasio::task<void> {
      co_await coasio::time::sleep(10ms);
      co_return;
    };

    auto int_task = []() -> coasio::task<int> {
      co_await coasio::time::sleep(15ms);
      co_return 7;
    };

    auto res = co_await join(void_task(), int_task());
    REQUIRE(res.has_value());
    static_assert(
        std::is_same_v<decltype(std::get<0>(*res)), std::monostate &>);
    REQUIRE(std::get<1>(*res) == 7);
    co_return;
  }());
}

TEST_CASE("join: parent cancellation aborts and drains all branches",
          "[combinators][join]") {
  coasio::runtime rt{};

  std::atomic<bool> cleaned_up_a{false};
  std::atomic<bool> cleaned_up_b{false};

  auto h = rt.spawn(
      [](std::atomic<bool> *ca, std::atomic<bool> *cb) -> coasio::task<void> {
        auto long_sleep_a = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 1;
        };

        auto long_sleep_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 2;
        };

        auto res = co_await join(long_sleep_a(ca), long_sleep_b(cb));
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error() == coasio::error::cancelled);
        co_return;
      }(&cleaned_up_a, &cleaned_up_b));

  std::this_thread::sleep_for(20ms);
  h.abort();
  auto join_res = rt.block_on(h.join());

  REQUIRE(join_res.has_value());
  REQUIRE(cleaned_up_a.load(std::memory_order_acquire));
  REQUIRE(cleaned_up_b.load(std::memory_order_acquire));
}

TEST_CASE("join: pre-cancelled parent returns error::cancelled cleanly",
          "[combinators][join]") {
  coasio::runtime rt{};

  std::atomic<bool> cleaned_up_a{false};
  std::atomic<bool> cleaned_up_b{false};

  auto h = rt.spawn(
      [](std::atomic<bool> *ca, std::atomic<bool> *cb) -> coasio::task<void> {
        co_await coasio::time::sleep(50ms);

        auto sleep_a = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 1;
        };

        auto sleep_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 2;
        };

        auto res = co_await join(sleep_a(ca), sleep_b(cb));
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error() == coasio::error::cancelled);
        co_return;
      }(&cleaned_up_a, &cleaned_up_b));

  h.abort();
  auto join_res = rt.block_on(h.join());

  REQUIRE(join_res.has_value());
  REQUIRE(cleaned_up_a.load(std::memory_order_acquire));
  REQUIRE(cleaned_up_b.load(std::memory_order_acquire));
}

TEST_CASE("join: exception in one branch aborts and drains remaining branches",
          "[combinators][join]") {
  std::atomic<bool> loser_cleaned_up{false};

  REQUIRE_THROWS_AS(
      sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
        auto failing = []() -> coasio::task<int> {
          co_await coasio::time::sleep(10ms);
          throw std::runtime_error("join failure");
          co_return 0;
        };

        auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value()) {
            co_await coasio::time::sleep(0ms);
            flag->store(true, std::memory_order_release);
          }
          co_return 0;
        };

        co_await join(failing(), slow(cleaned));
        co_return;
      }(&loser_cleaned_up)),
      std::runtime_error);

  REQUIRE(loser_cleaned_up.load(std::memory_order_acquire));
}

TEST_CASE("join: non-cooperative branch blocks join return",
          "[combinators][join]") {
  coasio::runtime rt{};

  auto [gate_tx, gate_rx] = coasio::sync::make_oneshot<int>();

  std::atomic<bool> join_returned{false};
  std::atomic<bool> branch_b_finished{false};

  auto gate_tx_ptr =
      std::make_shared<coasio::sync::oneshot::sender<int>>(std::move(gate_tx));

  auto h = rt.spawn([](coasio::sync::oneshot::receiver<int> rx,
                       std::atomic<bool> *returned,
                       std::atomic<bool> *b_finished) -> coasio::task<void> {
    auto fast = []() -> coasio::task<int> {
      co_await coasio::time::sleep(0ms);
      co_return 1;
    };

    auto blocking = [](coasio::sync::oneshot::receiver<int> gate,
                       std::atomic<bool> *finished) -> coasio::task<int> {
      auto val = co_await coasio::detail::uncancellable(std::move(gate));
      finished->store(true, std::memory_order_release);
      if (val.has_value())
        co_return *val;
      co_return -1;
    };

    auto res = co_await join(fast(), blocking(std::move(rx), b_finished));
    REQUIRE(res.has_value());
    REQUIRE(std::get<0>(*res) == 1);
    REQUIRE(std::get<1>(*res) == 999);
    returned->store(true, std::memory_order_release);
    co_return;
  }(std::move(gate_rx), &join_returned, &branch_b_finished));

  std::this_thread::sleep_for(50ms);
  REQUIRE_FALSE(join_returned.load(std::memory_order_acquire));
  REQUIRE_FALSE(branch_b_finished.load(std::memory_order_acquire));

  auto send_res = gate_tx_ptr->send(999);
  REQUIRE(send_res.has_value());

  auto join_res = rt.block_on(h.join());
  REQUIRE(join_res.has_value());

  REQUIRE(join_returned.load(std::memory_order_acquire));
  REQUIRE(branch_b_finished.load(std::memory_order_acquire));
}

TEST_CASE("join: completion vs parent cancel race stress test",
          "[combinators][join][stress]") {
  coasio::runtime rt{};
  for (int iter = 0; iter < 500; ++iter) {
    std::atomic<bool> branch_b_alive{false};

    auto h = rt.spawn([](std::atomic<bool> *alive) -> coasio::task<int> {
      auto fast = []() -> coasio::task<int> {
        co_await coasio::time::sleep(0ms);
        co_return 42;
      };

      auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
        struct Guard {
          std::atomic<bool> *ptr;
          ~Guard() { ptr->store(false, std::memory_order_release); }
        } guard{flag};

        flag->store(true, std::memory_order_release);
        co_await coasio::time::sleep(5s);
        co_return 99;
      };

      auto res = co_await join(fast(), slow(alive));

      if (res.has_value()) {
        co_return std::get<0>(*res) + std::get<1>(*res);
      }
      co_return -2;
    }(&branch_b_alive));

    h.abort();

    auto join_res = rt.block_on(h.join());
    REQUIRE(join_res.has_value());

    int val = *join_res;
    REQUIRE((val == (42 + 99) || val == -2));

    REQUIRE_FALSE(branch_b_alive.load(std::memory_order_acquire));
  }
}
