#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <coasio.hpp>
#include <coasio/time.hpp>

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

// Scheduling

TEST_CASE("block_on runs a task and returns its value", "[runtime]") {
  coasio::runtime rt{};
  int result = rt.block_on([]() -> coasio::task<int> { co_return 42; }());
  REQUIRE(result == 42);
}

TEST_CASE("spawn runs on a worker thread, not the calling thread",
          "[runtime]") {
  coasio::runtime rt{};
  std::atomic<std::thread::id> worker_id{};
  auto main_id = std::this_thread::get_id();

  auto h = rt.spawn([](std::atomic<std::thread::id>* wid) -> coasio::task<void> {
    wid->store(std::this_thread::get_id(), std::memory_order_release);
    co_return;
  }(&worker_id));

  rt.block_on(h.join());
  REQUIRE(worker_id.load() != std::thread::id{});
  REQUIRE(worker_id.load() != main_id);
}

TEST_CASE("multiple spawned tasks all complete", "[runtime]") {
  constexpr int N = 20;
  coasio::runtime rt{};
  std::atomic<int> count{0};

  std::vector<coasio::JoinHandle<void>> handles;
  for (int i = 0; i < N; ++i) {
    handles.push_back(rt.spawn([](std::atomic<int>* c) -> coasio::task<void> {
      c->fetch_add(1, std::memory_order_relaxed);
      co_return;
    }(&count)));
  }

  for (auto &h : handles)
    rt.block_on(h.join());
  REQUIRE(count.load() == N);
}

// JoinHandle

TEST_CASE("join returns the task's return value", "[runtime]") {
  coasio::runtime rt{};
  auto h = rt.spawn([]() -> coasio::task<int> { co_return 7; }());
  auto r = rt.block_on(h.join());
  REQUIRE(r.has_value());
  REQUIRE(*r == 7);
}

TEST_CASE("abort causes long sleep to return early", "[runtime]") {
  coasio::runtime rt{};

  std::atomic sleep_was_cancelled{false};

  auto h = rt.spawn([](std::atomic<bool>* swc) -> coasio::task<void> {
    const auto r = co_await coasio::time::sleep(
        std::chrono::minutes(60)); // 60 minutes to triger cmake timeout in case
    swc->store(!r.has_value(), std::memory_order_relaxed);
    co_return;
  }(&sleep_was_cancelled));

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  h.abort();
  auto r = rt.block_on(h.join());

  REQUIRE(r.has_value());
  REQUIRE(sleep_was_cancelled.load());
}

// Cancellation propagation

TEST_CASE("aborting a task interrupts its pending join() but leaves the "
          "spawned child running",
          "[runtime][cancel]") {
  coasio::runtime rt{};
  std::atomic<bool> child_finished{false};

  auto h =
      rt.spawn([](std::atomic<bool> *child_finished_ptr) -> coasio::task<void> {
        auto child =
            coasio::spawn([](std::atomic<bool> *ptr) -> coasio::task<void> {
              co_await coasio::time::sleep(std::chrono::seconds(2));
              ptr->store(true, std::memory_order_relaxed);
              co_return;
            }(child_finished_ptr));

        auto r = co_await child.join();
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == coasio::error::cancelled);
        co_return;
      }(&child_finished));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  h.abort();
  rt.block_on(h.join());

  // Wait for the child to finish its 50ms sleep.
  // If it didn't, CTest's timeout will catch it
  while (!child_finished.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(child_finished.load());
}

// Timer / sleep

TEST_CASE("sleep returns true after the deadline", "[runtime][time]") {
  sync_run([]() -> coasio::task<void> {
    auto r = co_await coasio::time::sleep(std::chrono::milliseconds(1));
    REQUIRE(r.has_value());
  }());
}

TEST_CASE("sleep cancelled by scope abort returns cancelled error",
          "[runtime][time]") {
  coasio::runtime rt{};
  auto h = rt.spawn([]() -> coasio::task<void> {
    auto r = co_await coasio::time::sleep(std::chrono::seconds(60));
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == asio::error::operation_aborted);
    co_return;
  }());

  rt.block_on([]() -> coasio::task<void> {
    co_await coasio::time::sleep(std::chrono::milliseconds(5));
  }());
  h.abort();
  rt.block_on(h.join());
}

// Runtime lifetime

TEST_CASE("runtime destruction cancels and cleans up all live tasks",
          "[runtime]") {
  std::atomic<bool> destroyed{false};

  struct Guard {
    std::atomic<bool> &flag;
    ~Guard() { flag.store(true, std::memory_order_relaxed); }
  };

  {
    coasio::runtime rt{};
    rt.spawn([&]() -> coasio::task<void> {
      Guard g{destroyed};
      co_await coasio::time::sleep(std::chrono::seconds(60));
      co_return;
    }());
    // rt goes out of scope -> cancels then waits for all tasks
  }

  REQUIRE(destroyed.load());
}
