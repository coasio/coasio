#ifndef COASIO_IO_BUFFER_MUT_HPP
#define COASIO_IO_BUFFER_MUT_HPP

#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

#include "coasio/io/concepts.hpp"

namespace coasio::io {

class bytes_mut {
public:
  explicit bytes_mut(const std::size_t initial_capacity = 4096)
      : storage_{initial_capacity} {}
  ~bytes_mut() = default;

  bytes_mut(bytes_mut &&o) noexcept
      : storage_{std::move(o.storage_)},
        read_idx_(std::exchange(o.read_idx_, 0)),
        write_idx_(std::exchange(o.write_idx_, 0)) {}

  bytes_mut &operator=(bytes_mut &&o) noexcept {
    if (this != &o) {
      storage_ = std::move(o.storage_);
      read_idx_ = std::exchange(o.read_idx_, 0);
      write_idx_ = std::exchange(o.write_idx_, 0);
    }
    return *this;
  }

  bytes_mut(const bytes_mut &) = delete;
  bytes_mut &operator=(const bytes_mut &) = delete;

  [[nodiscard]] std::span<const std::byte> readable_span() const noexcept {
    return {storage_.data() + read_idx_, write_idx_ - read_idx_};
  }

  [[nodiscard]] std::size_t readable_bytes() const noexcept {
    return write_idx_ - read_idx_;
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return storage_.size();
  }

  [[nodiscard]] bool empty() const noexcept { return read_idx_ == write_idx_; }

  [[nodiscard]] std::span<std::byte>
  writable_span(const std::size_t min_bytes = 0) {
    if (min_bytes > storage_.size() - write_idx_) {
      storage_.resize(write_idx_ + min_bytes);
    }
    return {storage_.data() + write_idx_, storage_.size() - write_idx_};
  }

  void commit(const std::size_t bytes_written) noexcept {
    write_idx_ += bytes_written;
  }

  void advance(const std::size_t bytes_consumed) noexcept {
    read_idx_ += bytes_consumed;
    if (read_idx_ == write_idx_) {
      clear();
    } else if (read_idx_ >= storage_.size() / 2) {
      std::memcpy(storage_.data(), storage_.data() + read_idx_,
                  write_idx_ - read_idx_);
      write_idx_ -= read_idx_;
      read_idx_ = 0;
    }
  }

  void clear() noexcept {
    read_idx_ = 0;
    write_idx_ = 0;
  }

  void reserve(const std::size_t additional) {
    storage_.resize(storage_.size() + additional);
  }

private:
  std::vector<std::byte> storage_;
  size_t read_idx_{0};
  size_t write_idx_{0};
};

static_assert(dynamic_buffer<bytes_mut>);

} // namespace coasio::io

#endif // !COASIO_IO_BUFFER_MUT_HPP