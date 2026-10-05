#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <coasio.hpp>
#include <coasio/sync/semaphore.hpp>
#include <coasio/time.hpp>

using namespace coasio::sync;

template <typename T> T sync_run(coasio::task<T> t) {
  coasio::runtime rt{};
  return rt.block_on(std::move(t));
}

// try_acquire (sync fast path)

TEST_CASE("try_acquire basic", "[sync][semaphore]") {
  semaphore sem(3);

  auto p = sem.try_acquire(2);
  REQUIRE(p.has_value());
  REQUIRE(p->count() == 2);
  REQUIRE(sem.available_permits() == 1);

  auto p2 = sem.try_acquire(2);
  REQUIRE_FALSE(p2.has_value());
}

TEST_CASE("try_acquire zero permits always succeeds", "[sync][semaphore]") {
  semaphore sem(0);
  auto p = sem.try_acquire(0);
  REQUIRE(p.has_value());
  REQUIRE(sem.available_permits() == 0);
}

TEST_CASE("try_acquire over capacity fails", "[sync][semaphore]") {
  semaphore sem(2);
  REQUIRE_FALSE(sem.try_acquire(3).has_value());
}

// permit RAII

TEST_CASE("permit releases on destruction", "[sync][semaphore]") {
  semaphore sem(2);
  {
    auto p = sem.try_acquire(2);
    REQUIRE(sem.available_permits() == 0);
  }
  REQUIRE(sem.available_permits() == 2);
}

TEST_CASE("permit forget skips release", "[sync][semaphore]") {
  semaphore sem(3);
  auto p = sem.try_acquire(2);
  REQUIRE(p.has_value());
  p->forget();
  p.reset();
  REQUIRE(sem.available_permits() == 1);
}

TEST_CASE("permit merge", "[sync][semaphore]") {
  semaphore sem(5);
  auto a = sem.try_acquire(2);
  auto b = sem.try_acquire(3);
  REQUIRE((a && b));
  a->merge(*b);
  REQUIRE(a->count() == 5);
  REQUIRE(b->count() == 0);
  b.reset();
  REQUIRE(sem.available_permits() == 0); // a still holds 5
}

TEST_CASE("permit move transfers ownership", "[sync][semaphore]") {
  semaphore sem(2);
  auto a = sem.try_acquire(2); // optional<semaphore_permit>
  {
    semaphore_permit b(std::move(*a));
    a.reset();
    REQUIRE(sem.available_permits() == 0);
  }
  REQUIRE(sem.available_permits() == 2);
}

// async acquire

TEST_CASE("async acquire: permits available immediately", "[sync][semaphore]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(1);
    auto p = co_await sem.acquire();
    REQUIRE(p.has_value());
    REQUIRE(sem.available_permits() == 0);
  }());
}

TEST_CASE("async acquire: waiter unblocked by release", "[sync][semaphore]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);

    auto h = coasio::spawn([](semaphore &sem) -> coasio::task<void> {
      auto p = co_await sem.acquire();
      REQUIRE(p.has_value());
      co_return;
    }(sem));

    co_await coasio::time::sleep(std::chrono::milliseconds(5));
    sem.release(1);
    auto r = co_await h.join();
    REQUIRE(r.has_value());
  }());
}

TEST_CASE("async acquire multi-permit waiter", "[sync][semaphore]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);

    auto h = coasio::spawn([](semaphore &sem) -> coasio::task<void> {
      auto p = co_await sem.acquire(3);
      REQUIRE(p.has_value());
      REQUIRE(p->count() == 3);
      co_return;
    }(sem));

    co_await coasio::time::sleep(std::chrono::milliseconds(5));
    sem.release(3);
    auto r = co_await h.join();
    REQUIRE(r.has_value());
  }());
}

// Cancellation

TEST_CASE("acquire cancelled while waiting", "[sync][semaphore][cancel]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);

    auto h = coasio::spawn([](semaphore *s) -> coasio::task<void> {
      auto p = co_await s->acquire();
      REQUIRE_FALSE(p.has_value());
      REQUIRE(p.error() == coasio::error::cancelled);
      co_return;
    }(&sem));

    co_await coasio::time::sleep(std::chrono::milliseconds(5));
    h.abort();
    co_await h.join();
    // Permits should not be consumed by a cancelled waiter.
    REQUIRE(sem.available_permits() == 0);
  }());
}

TEST_CASE("acquire pre-cancelled by scope", "[sync][semaphore][cancel]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);

    auto h = coasio::spawn([](semaphore *s) -> coasio::task<void> {
      auto p = co_await s->acquire();
      REQUIRE_FALSE(p.has_value());
      co_return;
    }(&sem));

    h.abort();
    co_await h.join();
  }());
}

TEST_CASE("release after cancelled waiter goes to next waiter",
          "[sync][semaphore][cancel]") {
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);
    std::atomic<bool> second_got{false};

    auto h1 = coasio::spawn([](semaphore *s) -> coasio::task<void> {
      auto p = co_await s->acquire();
      co_return;
    }(&sem));

    auto h2 = coasio::spawn(
        [](semaphore *s, std::atomic<bool> *got) -> coasio::task<void> {
          auto p = co_await s->acquire();
          if (p.has_value())
            got->store(true, std::memory_order_relaxed);
          co_return;
        }(&sem, &second_got));

    co_await coasio::time::sleep(std::chrono::milliseconds(10));
    h1.abort();
    co_await coasio::time::sleep(std::chrono::milliseconds(2));

    sem.release(1); // should wake h2
    co_await h2.join();

    REQUIRE(second_got.load());
  }());
}

// Stress

TEST_CASE("stress: many concurrent acquirers", "[sync][semaphore][stress]") {
  constexpr int N = 100;
  sync_run([]() -> coasio::task<void> {
    semaphore sem(0);
    std::atomic<int> done{0};

    std::vector<coasio::JoinHandle<void>> handles;
    handles.reserve(N);
    for (int i = 0; i < N; ++i) {
      handles.push_back(coasio::spawn(
          [](semaphore *s, std::atomic<int> *d) -> coasio::task<void> {
            auto p = co_await s->acquire();
            REQUIRE(p.has_value());
            d->fetch_add(1, std::memory_order_relaxed);
            p->forget();
            co_return;
          }(&sem, &done)));
    }

    co_await coasio::time::sleep(std::chrono::milliseconds(5));
    sem.release(N);

    for (auto &h : handles)
      co_await h.join();
    REQUIRE(done.load() == N);
  }());
}
