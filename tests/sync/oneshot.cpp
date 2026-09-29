#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

#include <coasio.hpp>
#include <coasio/sync/oneshot.hpp>
#include <coasio/time.hpp>

using namespace coasio::sync;

template <typename T>
T sync_run(coasio::task<T> t) {
    coasio::runtime rt{};
    return rt.block_on(std::move(t));
}

// ── Basic send / receive ──────────────────────────────────────────────────────

TEST_CASE("send before receiver awaits", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        REQUIRE(tx.send(1).has_value());
        auto res = co_await std::move(rx);
        REQUIRE(res.has_value());
        REQUIRE(*res == 1);
    }());
}

TEST_CASE("receiver awaits before send", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        coasio::spawn([](oneshot::sender<int> tx) -> coasio::task<void> {
            co_await coasio::time::sleep(std::chrono::milliseconds(1));
            REQUIRE(tx.send(42).has_value());
            co_return;
        }(std::move(tx)));
        auto res = co_await std::move(rx);
        REQUIRE(res.has_value());
        REQUIRE(*res == 42);
    }());
}

TEST_CASE("move-only value type", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<std::unique_ptr<int>>();
        REQUIRE(tx.send(std::make_unique<int>(7)).has_value());
        auto res = co_await std::move(rx);
        REQUIRE(res.has_value());
        REQUIRE(**res == 7);
    }());
}

TEST_CASE("zero and false values are not confused with errors", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        REQUIRE(tx.send(0).has_value());
        auto res = co_await std::move(rx);
        REQUIRE(res.has_value());
        REQUIRE(*res == 0);
    }());
}

// ── Drop semantics ────────────────────────────────────────────────────────────

TEST_CASE("sender dropped: receiver gets channel_closed", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        { auto _ = std::move(tx); }
        auto res = co_await std::move(rx);
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error() == coasio::error::channel_closed);
    }());
}

TEST_CASE("sender dropped while receiver is suspended", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        coasio::spawn([](oneshot::sender<int> tx) -> coasio::task<void> {
            co_await coasio::time::sleep(std::chrono::milliseconds(1));
            co_return; // tx drops here
        }(std::move(tx)));
        auto res = co_await std::move(rx);
        REQUIRE_FALSE(res.has_value());
        REQUIRE(res.error() == coasio::error::channel_closed);
    }());
}

TEST_CASE("receiver dropped: send returns value back", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        { auto _ = std::move(rx); }
        auto bounce = tx.send(99);
        REQUIRE_FALSE(bounce.has_value());
        REQUIRE(bounce.error() == 99);
        co_return;
    }());
}

TEST_CASE("is_closed reflects receiver drop", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        REQUIRE_FALSE(tx.is_closed());
        { auto _ = std::move(rx); }
        REQUIRE(tx.is_closed());
        co_return;
    }());
}

TEST_CASE("second send bounces value back", "[sync][oneshot]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        REQUIRE(tx.send(1).has_value());
        auto bounce = tx.send(2);
        REQUIRE_FALSE(bounce.has_value());
        REQUIRE(bounce.error() == 2);
        auto res = co_await std::move(rx);
        REQUIRE(res.has_value());
        REQUIRE(*res == 1);
    }());
}

// ── Cancellation ──────────────────────────────────────────────────────────────

TEST_CASE("cancel before receiver awaits", "[sync][oneshot][cancel]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        auto h = coasio::spawn([](oneshot::receiver<int> rx) -> coasio::task<void> {
            auto res = co_await std::move(rx);
            REQUIRE_FALSE(res.has_value());
            co_return;
        }(std::move(rx)));
        h.abort();
        (void)tx;
        co_await h.join();
    }());
}

TEST_CASE("cancel while receiver is suspended", "[sync][oneshot][cancel]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        auto h = coasio::spawn([](oneshot::receiver<int> rx) -> coasio::task<void> {
            auto res = co_await std::move(rx);
            REQUIRE_FALSE(res.has_value());
            REQUIRE(res.error() == coasio::error::cancelled);
            co_return;
        }(std::move(rx)));

        co_await coasio::time::sleep(std::chrono::milliseconds(5));
        h.abort();
        co_await coasio::time::sleep(std::chrono::milliseconds(1));
        REQUIRE(tx.is_closed());
        co_await h.join();
    }());
}

TEST_CASE("send after cancel bounces value", "[sync][oneshot][cancel]") {
    sync_run([]() -> coasio::task<void> {
        auto [tx, rx] = make_oneshot<int>();
        auto h = coasio::spawn([](oneshot::receiver<int> rx) -> coasio::task<void> {
            auto res = co_await std::move(rx);
            REQUIRE_FALSE(res.has_value());
            co_return;
        }(std::move(rx)));
        h.abort();
        co_await h.join();
        auto bounce = tx.send(55);
        REQUIRE_FALSE(bounce.has_value());
        REQUIRE(bounce.error() == 55);
    }());
}

// ── Stress ────────────────────────────────────────────────────────────────────

TEST_CASE("stress: concurrent channels", "[sync][oneshot][stress]") {
    constexpr int N = 200;
    sync_run([]() -> coasio::task<void> {
        std::vector<oneshot::receiver<int>> receivers;
        receivers.reserve(N);
        for (int i = 0; i < N; ++i) {
            auto [tx, rx] = make_oneshot<int>();
            receivers.push_back(std::move(rx));
            coasio::spawn([](oneshot::sender<int> tx, int val) -> coasio::task<void> {
                co_await coasio::time::sleep(std::chrono::milliseconds(0));
                tx.send(val);
                co_return;
            }(std::move(tx), i));
        }
        int sum = 0;
        for (auto &rx : receivers) {
            auto res = co_await std::move(rx);
            REQUIRE(res.has_value());
            sum += *res;
        }
        REQUIRE(sum == N * (N - 1) / 2);
    }());
}

TEST_CASE("stress: send-receive race regression", "[sync][oneshot][stress]") {
    constexpr int ROUNDS = 500;
    for (int i = 0; i < ROUNDS; ++i) {
        coasio::runtime rt{};
        auto [tx, rx] = make_oneshot<int>();

        auto sh = rt.spawn([](oneshot::sender<int> tx) -> coasio::task<void> {
            tx.send(1);
            co_return;
        }(std::move(tx)));

        auto rh = rt.spawn([](oneshot::receiver<int> rx) -> coasio::task<void> {
            auto res = co_await std::move(rx);
            REQUIRE(res.has_value());
            REQUIRE(*res == 1);
            co_return;
        }(std::move(rx)));

        rt.block_on(sh.join());
        rt.block_on(rh.join());
    }
}