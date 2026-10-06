#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <variant>

#include <coasio.hpp>
#include <coasio/combinators/select.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

TEST_CASE("select: faster branch wins and slower branch is aborted",
          "[combinators][select]") {
  std::atomic<bool> slow_cancelled{false};

  auto fast_task = []() -> coasio::task<int> {
    co_await coasio::time::sleep(10ms);
    co_return 42;
  };

  auto slow_task = [](std::atomic<bool> *cancelled) -> coasio::task<int> {
    auto r = co_await coasio::time::sleep(200ms);
    if (!r.has_value()) {
      cancelled->store(true, std::memory_order_release);
    }
    co_return 99;
  };

  sync_run([&]() -> coasio::task<void> {
    auto res = co_await coasio::select(fast_task(), slow_task(&slow_cancelled));

    REQUIRE(res.index() == 0);
    auto *b0 = std::get_if<coasio::branch_result<0, int>>(&res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value == 42);
    co_return;
  }());

  REQUIRE(slow_cancelled.load(std::memory_order_acquire));
}

TEST_CASE("select: second branch can win", "[combinators][select]") {
  std::atomic<bool> slow_cancelled{false};

  auto slow_task = [](std::atomic<bool> *cancelled) -> coasio::task<int> {
    auto r = co_await coasio::time::sleep(200ms);
    if (!r.has_value()) {
      cancelled->store(true, std::memory_order_release);
    }
    co_return 10;
  };

  auto fast_task = []() -> coasio::task<int> {
    co_await coasio::time::sleep(10ms);
    co_return 20;
  };

  sync_run([&]() -> coasio::task<void> {
    auto res = co_await coasio::select(slow_task(&slow_cancelled), fast_task());

    REQUIRE(res.index() == 1);
    auto *b1 = std::get_if<coasio::branch_result<1, int>>(&res);
    REQUIRE(b1 != nullptr);
    REQUIRE(b1->value == 20);
    co_return;
  }());

  REQUIRE(slow_cancelled.load(std::memory_order_acquire));
}

TEST_CASE("select: supports identical return types", "[combinators][select]") {
  sync_run([]() -> coasio::task<void> {
    auto t0 = []() -> coasio::task<std::string> {
      co_await coasio::time::sleep(10ms);
      co_return "first";
    };

    auto t1 = []() -> coasio::task<std::string> {
      co_await coasio::time::sleep(100ms);
      co_return "second";
    };

    auto res = co_await coasio::select(t0(), t1());
    REQUIRE(res.index() == 0);

    auto *b0 = std::get_if<coasio::branch_result<0, std::string>>(&res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value == "first");
    co_return;
  }());
}

TEST_CASE("select: supports heterogeneous return types",
          "[combinators][select]") {
  sync_run([]() -> coasio::task<void> {
    auto t0 = []() -> coasio::task<std::string> {
      co_await coasio::time::sleep(10ms);
      co_return "hello";
    };

    auto t1 = []() -> coasio::task<int> {
      co_await coasio::time::sleep(100ms);
      co_return 123;
    };

    auto res = co_await coasio::select(t0(), t1());
    REQUIRE(res.index() == 0);

    auto *b0 = std::get_if<coasio::branch_result<0, std::string>>(&res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value == "hello");
    co_return;
  }());
}

TEST_CASE("select: supports void return types", "[combinators][select]") {
  sync_run([]() -> coasio::task<void> {
    auto t0 = []() -> coasio::task<void> {
      co_await coasio::time::sleep(10ms);
      co_return;
    };

    auto t1 = []() -> coasio::task<int> {
      co_await coasio::time::sleep(100ms);
      co_return 999;
    };

    auto res = co_await coasio::select(t0(), t1());
    REQUIRE(res.index() == 0);
    REQUIRE(std::holds_alternative<coasio::branch_result<0, void>>(res));
    co_return;
  }());
}

TEST_CASE("select: supports all void return types", "[combinators][select]") {
  sync_run([]() -> coasio::task<void> {
    auto t0 = []() -> coasio::task<void> {
      co_await coasio::time::sleep(100ms);
      co_return;
    };

    auto t1 = []() -> coasio::task<void> {
      co_await coasio::time::sleep(10ms);
      co_return;
    };

    auto res = co_await coasio::select(t0(), t1());
    REQUIRE(res.index() == 1);
    REQUIRE(std::holds_alternative<coasio::branch_result<1, void>>(res));
    co_return;
  }());
}

TEST_CASE("select: supports move-only return types", "[combinators][select]") {
  sync_run([]() -> coasio::task<void> {
    auto t0 = []() -> coasio::task<std::unique_ptr<int>> {
      co_await coasio::time::sleep(10ms);
      co_return std::make_unique<int>(777);
    };

    auto t1 = []() -> coasio::task<int> {
      co_await coasio::time::sleep(100ms);
      co_return 888;
    };

    auto res = co_await coasio::select(t0(), t1());
    REQUIRE(res.index() == 0);

    auto *b0 =
        std::get_if<coasio::branch_result<0, std::unique_ptr<int>>>(&res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value != nullptr);
    REQUIRE(*b0->value == 777);
    co_return;
  }());
}

TEST_CASE("select: selects among 3 or more tasks", "[combinators][select]") {
  std::atomic<bool> cancelled0{false};
  std::atomic<bool> cancelled2{false};

  auto t0 = [](std::atomic<bool> *c) -> coasio::task<int> {
    auto r = co_await coasio::time::sleep(100ms);
    if (!r.has_value())
      c->store(true, std::memory_order_release);
    co_return 1;
  };

  auto t1 = []() -> coasio::task<int> {
    co_await coasio::time::sleep(10ms);
    co_return 2;
  };

  auto t2 = [](std::atomic<bool> *c) -> coasio::task<int> {
    auto r = co_await coasio::time::sleep(150ms);
    if (!r.has_value())
      c->store(true, std::memory_order_release);
    co_return 3;
  };

  sync_run([&]() -> coasio::task<void> {
    auto res = co_await coasio::select(t0(&cancelled0), t1(), t2(&cancelled2));

    REQUIRE(res.index() == 1);
    auto *b1 = std::get_if<coasio::branch_result<1, int>>(&res);
    REQUIRE(b1 != nullptr);
    REQUIRE(b1->value == 2);
    co_return;
  }());

  REQUIRE(cancelled0.load(std::memory_order_acquire));
  REQUIRE(cancelled2.load(std::memory_order_acquire));
}

TEST_CASE("select: rethrows exception if winning task fails",
          "[combinators][select]") {
  std::atomic<bool> slow_cancelled{false};

  auto failing_task = []() -> coasio::task<int> {
    co_await coasio::time::sleep(10ms);
    throw std::runtime_error("winner failed");
    co_return 0;
  };

  auto slow_task = [](std::atomic<bool> *c) -> coasio::task<int> {
    auto r = co_await coasio::time::sleep(150ms);
    if (!r.has_value())
      c->store(true, std::memory_order_release);
    co_return 42;
  };

  REQUIRE_THROWS_AS(sync_run([&]() -> coasio::task<void> {
                      co_await coasio::select(failing_task(),
                                              slow_task(&slow_cancelled));
                    }()),
                    std::runtime_error);

  REQUIRE(slow_cancelled.load(std::memory_order_acquire));
}

TEST_CASE("select: suppresses exception in slower losing task",
          "[combinators][select]") {
  auto fast_task = []() -> coasio::task<int> {
    co_await coasio::time::sleep(10ms);
    co_return 100;
  };

  auto failing_slow_task = []() -> coasio::task<int> {
    co_await coasio::time::sleep(100ms);
    throw std::runtime_error("loser failed");
    co_return 0;
  };

  sync_run([&]() -> coasio::task<void> {
    auto res = co_await coasio::select(fast_task(), failing_slow_task());
    REQUIRE(res.index() == 0);
    auto *b0 = std::get_if<coasio::branch_result<0, int>>(&res);
    REQUIRE(b0 != nullptr);
    REQUIRE(b0->value == 100);
    co_return;
  }());
}

TEST_CASE("select: parent abort cancels all child branches",
          "[combinators][select]") {
  coasio::runtime rt{};

  std::atomic<bool> c0{false};
  std::atomic<bool> c1{false};

  auto h = rt.spawn([&]() -> coasio::task<void> {
    auto t0 = [](std::atomic<bool> *c) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(1s);
      if (!r.has_value())
        c->store(true, std::memory_order_release);
      co_return 1;
    };
    auto t1 = [](std::atomic<bool> *c) -> coasio::task<int> {
      auto r = co_await coasio::time::sleep(1s);
      if (!r.has_value())
        c->store(true, std::memory_order_release);
      co_return 2;
    };

    try {
      co_await coasio::select(t0(&c0), t1(&c1));
    } catch (const std::system_error &e) {
      REQUIRE(e.code() == coasio::error::cancelled);
    }
  }());

  std::this_thread::sleep_for(15ms);
  h.abort();
  auto r = rt.block_on(h.join());
  REQUIRE(r.has_value());

  REQUIRE(c0.load(std::memory_order_acquire));
  REQUIRE(c1.load(std::memory_order_acquire));
}
