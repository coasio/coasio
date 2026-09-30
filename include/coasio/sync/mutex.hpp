#ifndef COASIO_SYNC_MUTEX_HPP
#define COASIO_SYNC_MUTEX_HPP

#include <optional>

#include "semaphore.hpp"

namespace coasio::sync {
class mutex_guard {
  semaphore_permit permit_;

public:
  explicit mutex_guard(semaphore_permit permit) : permit_(std::move(permit)) {}
  mutex_guard(mutex_guard &&) noexcept = default;
  mutex_guard &operator=(mutex_guard &&) noexcept = default;
  mutex_guard(const mutex_guard &) = delete;
  mutex_guard &operator=(const mutex_guard &) = delete;
  ~mutex_guard() = default;
};

class mutex {
  semaphore sem_{1};

public:
  auto lock() noexcept { return sem_.acquire(1); }
  std::optional<mutex_guard> try_lock() noexcept {
    return sem_.try_acquire(1).transform([](semaphore_permit &&sem_per) {
      return mutex_guard{std::move(sem_per)};
    });
  }
};
} // namespace coasio::sync

#endif // !COASIO_SYNC_MUTEX_HPP
