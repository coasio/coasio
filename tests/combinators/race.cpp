#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <coasio.hpp>
#include <coasio/combinators/race.hpp>
#include <coasio/sync/oneshot.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;
using namespace coasio::combinators;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

TEST_CASE("race: first completing branch wins with direct value",
          "[combinators][race]") {
  sync_run([]() -> coasio::task<void> {
    auto fast = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };
    auto slow = []() -> coasio::task<int> {
      co_await coasio::time::sleep(5s);
      co_return 100;
    };

    auto res = co_await race(fast(), slow());
    REQUIRE(res.has_value());
    REQUIRE(*res == 42);
    co_return;
  }());
}

TEST_CASE("race: works with void return types", "[combinators][race]") {
  sync_run([]() -> coasio::task<void> {
    auto fast_void = []() -> coasio::task<void> {
      co_await coasio::time::sleep(10ms);
      co_return;
    };
    auto slow_void = []() -> coasio::task<void> {
      co_await coasio::time::sleep(5s);
      co_return;
    };

    auto res = co_await race(fast_void(), slow_void());
    REQUIRE(res.has_value());
    co_return;
  }());
}

TEST_CASE("race: supports common type conversion", "[combinators][race]") {
  sync_run([]() -> coasio::task<void> {
    auto int_branch = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };
    auto double_branch = []() -> coasio::task<double> {
      co_await coasio::time::sleep(5s);
      co_return 3.14;
    };

    auto res = co_await race(int_branch(), double_branch());
    REQUIRE(res.has_value());
    REQUIRE(*res == 42.0);
    static_assert(std::is_same_v<decltype(*res), double &>);
    co_return;
  }());
}

TEST_CASE("race: waits for loser cleanup before returning",
          "[combinators][race]") {
  std::atomic<bool> loser_cleaned_up{false};

  sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
    auto fast = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };

    auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        co_await coasio::time::sleep(0ms);
        flag->store(true, std::memory_order_release);
      }
      co_return 0;
    };

    auto res = co_await race(fast(), slow(cleaned));
    REQUIRE(res.has_value());
    REQUIRE(*res == 42);
    REQUIRE(cleaned->load(std::memory_order_acquire));
    co_return;
  }(&loser_cleaned_up));
}

TEST_CASE("race: parent cancellation drains all branches",
          "[combinators][race]") {
  coasio::runtime rt{};

  std::atomic<bool> cleaned_up_a{false};
  std::atomic<bool> cleaned_up_b{false};

  auto h = rt.spawn(
      [](std::atomic<bool> *ca, std::atomic<bool> *cb) -> coasio::task<void> {
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

        auto res = co_await race(sleep_a(ca), sleep_b(cb));
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

TEST_CASE("race: pre-cancelled parent returns error::cancelled",
          "[combinators][race]") {
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

        auto res = co_await race(sleep_a(ca), sleep_b(cb));
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

TEST_CASE("race: exception in branch aborts and drains losers",
          "[combinators][race]") {
  std::atomic<bool> loser_cleaned_up{false};

  REQUIRE_THROWS_AS(
      sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
        auto failing = []() -> coasio::task<int> {
          co_await coasio::time::sleep(10ms);
          throw std::runtime_error("race failure");
          co_return 0;
        };

        auto slow = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 0;
        };

        co_await race(failing(), slow(cleaned));
        co_return;
      }(&loser_cleaned_up)),
      std::runtime_error);

  REQUIRE(loser_cleaned_up.load(std::memory_order_acquire));
}

TEST_CASE("race: non-cooperative loser blocks combinator return",
          "[combinators][race]") {
  coasio::runtime rt{};

  auto [gate_tx, gate_rx] = coasio::sync::make_oneshot<int>();

  std::atomic<bool> race_returned{false};
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

    auto res = co_await race(fast(), blocking(std::move(rx), b_finished));
    REQUIRE(res.has_value());
    REQUIRE(*res == 1);
    returned->store(true, std::memory_order_release);
    co_return;
  }(std::move(gate_rx), &race_returned, &branch_b_finished));

  std::this_thread::sleep_for(50ms);
  REQUIRE_FALSE(race_returned.load(std::memory_order_acquire));
  REQUIRE_FALSE(branch_b_finished.load(std::memory_order_acquire));

  auto send_res = gate_tx_ptr->send(999);
  REQUIRE(send_res.has_value());

  auto join_res = rt.block_on(h.join());
  REQUIRE(join_res.has_value());

  REQUIRE(race_returned.load(std::memory_order_acquire));
  REQUIRE(branch_b_finished.load(std::memory_order_acquire));
}

TEST_CASE("race: winner vs parent cancel stress race",
          "[combinators][race][stress]") {
  coasio::runtime rt{};
  for (int iter = 0; iter < 1000; ++iter) {
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

      auto res = co_await race(fast(), slow(alive));

      if (res.has_value()) {
        co_return *res;
      }
      co_return -2;
    }(&branch_b_alive));

    h.abort();

    auto join_res = rt.block_on(h.join());
    REQUIRE(join_res.has_value());

    int val = *join_res;
    REQUIRE((val == 42 || val == -2));

    REQUIRE_FALSE(branch_b_alive.load(std::memory_order_acquire));
  }
}
