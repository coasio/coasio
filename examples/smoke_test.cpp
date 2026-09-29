#include <chrono>
#include <iostream>

#include <coasio.hpp>
#include <coasio/net/tcp/socket.hpp>
#include <coasio/time.hpp>

using namespace std::chrono_literals;

coasio::task<void> basic_task(bool &ran) {
  ran = true;
  co_return;
}

coasio::task<void> delayed_task(bool &ran) {
  if (!(co_await coasio::time::sleep(10ms))) {
    co_return;
  }
  ran = true;
  co_return;
}

int _main() {
  coasio::runtime rt;
  coasio::runtime::context_guard guard{&rt};
  {
    bool ran = false;

    rt.spawn(basic_task(ran));
    rt.block_on([]() -> coasio::task<void> {
      co_await coasio::time::sleep(1s);
      co_return;
    }());

    assert(ran);
    std::cout << "[PASS] basic task\n";
  }
  {
    bool ran = false;

    auto start = std::chrono::steady_clock::now();

    rt.spawn(delayed_task(ran));
    rt.block_on([]() -> coasio::task<void> {
      co_await coasio::time::sleep(1s);
      co_return;
    }());

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    assert(ran);
    assert(elapsed >= 5ms);

    std::cout << "[PASS] timer + task (" << elapsed.count() << " ms)\n";
  }
  {
    int counter = 0;

    auto task = [&]() -> coasio::task<void> {
      ++counter;
      co_return;
    };

    rt.spawn(task());
    rt.spawn(task());
    rt.spawn(task());

    rt.block_on([]() -> coasio::task<void> {
      co_await coasio::time::sleep(1s);
      co_return;
    }());

    assert(counter == 3);

    std::cout << "[PASS] multiple tasks\n";
  }

  return 0;
}

int main() {
  coasio::runtime rt;
  rt.block_on([]() -> coasio::task<void> {
    bool b;
    for (size_t i = 0; i < 1'000'000; i++) {
      if (i % 100'000 == 0)
        std::cout << i << "\n";
      coasio::spawn(basic_task(b));
    }
    co_await coasio::time::sleep(2s);
    co_return;
  });
}
