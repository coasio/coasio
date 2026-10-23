#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <variant>

#include <coasio.hpp>
#include <coasio/combinators/select.hpp>
#include <coasio/sync/oneshot.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;
using namespace coasio::combinators;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

TEST_CASE("select_waits_for_loser_cleanup", "[combinators][select]") {
  std::atomic<bool> loser_cleaned_up{false};

  sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
    auto branch_a = []() -> coasio::task<int> {
      co_await coasio::time::sleep(10ms);
      co_return 42;
    };

    auto branch_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(5s);
      if (!r.has_value()) {
        // Simulate async cleanup work after cancellation
        co_await coasio::time::sleep(0ms);
        flag->store(true, std::memory_order_release);
      }
      co_return 0;
    };

    auto res = co_await select(branch_a(), branch_b(cleaned));

    REQUIRE(res.has_value());
    REQUIRE(cleaned->load(std::memory_order_acquire));

    auto *b0 = std::get_if<branch_result<0, int>>(&*res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value == 42);
    co_return;
  }(&loser_cleaned_up));
}

TEST_CASE("select_parent_cancelled_drains_all_branches",
          "[combinators][select]") {
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

        auto res = co_await select(long_sleep_a(ca), long_sleep_b(cb));

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

TEST_CASE("select_pre_cancelled_parent", "[combinators][select]") {
  coasio::runtime rt{};

  std::atomic<bool> cleaned_up_a{false};
  std::atomic<bool> cleaned_up_b{false};

  auto h = rt.spawn(
      [](std::atomic<bool> *ca, std::atomic<bool> *cb) -> coasio::task<void> {
        co_await coasio::time::sleep(50ms);

        auto branch_a = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 1;
        };

        auto branch_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 2;
        };

        auto res = co_await select(branch_a(ca), branch_b(cb));

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

TEST_CASE("select_winner_vs_parent_cancel_race",
          "[combinators][select][stress]") {
  coasio::runtime rt{};
  for (int iter = 0; iter < 1000; ++iter) {
    std::atomic<bool> branch_b_alive{false};

    auto h = rt.spawn([](std::atomic<bool> *alive) -> coasio::task<int> {
      auto fast_a = []() -> coasio::task<int> {
        co_await coasio::time::sleep(0ms);
        co_return 42;
      };

      auto slow_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
        struct Guard {
          std::atomic<bool> *ptr;
          ~Guard() { ptr->store(false, std::memory_order_release); }
        } guard{flag};

        flag->store(true, std::memory_order_release);
        co_await coasio::time::sleep(5s);
        co_return 99;
      };

      auto res = co_await select(fast_a(), slow_b(alive));

      if (res.has_value()) {
        auto *b0 = std::get_if<branch_result<0, int>>(&*res);
        if (b0)
          co_return b0->value;
        co_return -1;
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

TEST_CASE("select_exception_drains_losers", "[combinators][select]") {
  std::atomic<bool> loser_cleaned_up{false};

  REQUIRE_THROWS_AS(
      sync_run([](std::atomic<bool> *cleaned) -> coasio::task<void> {
        auto failing_a = []() -> coasio::task<int> {
          co_await coasio::time::sleep(10ms);
          throw std::runtime_error("boom");
          co_return 0;
        };

        auto slow_b = [](std::atomic<bool> *flag) -> coasio::task<int> {
          auto r = co_await coasio::time::sleep(5s);
          if (!r.has_value())
            flag->store(true, std::memory_order_release);
          co_return 0;
        };

        co_await select(failing_a(), slow_b(cleaned));
        co_return;
      }(&loser_cleaned_up)),
      std::runtime_error);

  REQUIRE(loser_cleaned_up.load(std::memory_order_acquire));
}

TEST_CASE("timeout_distinguishes_deadline_from_parent_cancel",
          "[combinators][select]") {

  SECTION("deadline expires") {
    std::atomic<bool> user_task_cancelled{false};

    sync_run([](std::atomic<bool> *cancelled) -> coasio::task<void> {
      auto user_task = [](std::atomic<bool> *flag) -> coasio::task<int> {
        auto r = co_await coasio::time::sleep(5s);
        if (!r.has_value())
          flag->store(true, std::memory_order_release);
        co_return 123;
      };

      auto res = co_await select(user_task(cancelled), coasio::time::sleep(20ms));

      REQUIRE(res.has_value());
      // Branch 1 (deadline) won
      REQUIRE(res->index() == 1);
      REQUIRE(std::holds_alternative<branch_result<1, std::expected<void, std::error_code>>>(*res));
      co_return;
    }(&user_task_cancelled));

    REQUIRE(user_task_cancelled.load(std::memory_order_acquire));
  }

  SECTION("parent cancel interrupts") {
    coasio::runtime rt{};
    std::atomic<bool> user_task_cancelled{false};
    std::atomic<bool> deadline_cancelled{false};

    auto h = rt.spawn(
        [](std::atomic<bool> *uc, std::atomic<bool> *dc) -> coasio::task<void> {
          auto user_task = [](std::atomic<bool> *flag) -> coasio::task<int> {
            auto r = co_await coasio::time::sleep(5s);
            if (!r.has_value())
              flag->store(true, std::memory_order_release);
            co_return 123;
          };

          auto deadline = [](std::atomic<bool> *flag) -> coasio::task<void> {
            auto r = co_await coasio::time::sleep(5s);
            if (!r.has_value())
              flag->store(true, std::memory_order_release);
            co_return;
          };

          auto res = co_await select(user_task(uc), deadline(dc));

          REQUIRE_FALSE(res.has_value());
          REQUIRE(res.error() == coasio::error::cancelled);
          co_return;
        }(&user_task_cancelled, &deadline_cancelled));

    std::this_thread::sleep_for(10ms);
    h.abort();
    auto join_res = rt.block_on(h.join());

    REQUIRE(join_res.has_value());
    REQUIRE(user_task_cancelled.load(std::memory_order_acquire));
    REQUIRE(deadline_cancelled.load(std::memory_order_acquire));
  }
}

TEST_CASE("select_waits_for_non_cooperative_loser", "[combinators][select]") {
  coasio::runtime rt{};

  auto [gate_tx, gate_rx] = coasio::sync::make_oneshot<int>();

  std::atomic<bool> select_returned{false};
  std::atomic<bool> branch_b_finished{false};

  auto gate_tx_ptr =
      std::make_shared<coasio::sync::oneshot::sender<int>>(std::move(gate_tx));

  auto h = rt.spawn([](coasio::sync::oneshot::receiver<int> rx,
                       std::atomic<bool> *returned,
                       std::atomic<bool> *b_finished) -> coasio::task<void> {
    auto fast_a = []() -> coasio::task<int> {
      co_await coasio::time::sleep(0ms);
      co_return 1;
    };

    auto blocking_b = [](coasio::sync::oneshot::receiver<int> gate,
                         std::atomic<bool> *finished) -> coasio::task<int> {
      auto val = co_await coasio::detail::uncancellable(std::move(gate));
      finished->store(true, std::memory_order_release);
      if (val.has_value())
        co_return *val;
      co_return -1;
    };

    co_await select(fast_a(), blocking_b(std::move(rx), b_finished));
    returned->store(true, std::memory_order_release);
    co_return;
  }(std::move(gate_rx), &select_returned, &branch_b_finished));

  std::this_thread::sleep_for(50ms);
  REQUIRE_FALSE(select_returned.load(std::memory_order_acquire));
  REQUIRE_FALSE(branch_b_finished.load(std::memory_order_acquire));

  auto send_res = gate_tx_ptr->send(999);
  REQUIRE(send_res.has_value());

  auto join_res = rt.block_on(h.join());
  REQUIRE(join_res.has_value());

  REQUIRE(select_returned.load(std::memory_order_acquire));
  REQUIRE(branch_b_finished.load(std::memory_order_acquire));
}
