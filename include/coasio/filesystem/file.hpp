#ifndef COASIO_FILESYSTEM_FILE_HPP
#define COASIO_FILESYSTEM_FILE_HPP

#include <expected>
#include <filesystem>
#include <ranges>
#include <span>
#include <stdexcept>

#include <asio/file_base.hpp>
#include <asio/random_access_file.hpp>
#include <asio/read.hpp>
#include <asio/read_at.hpp>
#include <asio/stream_file.hpp>
#include <asio/write.hpp>
#include <asio/write_at.hpp>

#include "coasio/detail/async_op.hpp"
#include "coasio/filesystem.hpp"
#include "coasio/task.hpp"

namespace coasio::filesystem {
enum class open_mode { read, write, read_write };

struct open_options {
  open_mode mode = open_mode::read;

  bool create = false;
  bool exclusive = false;
  bool truncate = false;
  bool append = false;

  bool sync_all_on_write = false;

  asio::file_base::flags into_asio_flags() const {
    asio::file_base::flags flags;
    switch (mode) {
    case open_mode::read:
      flags = asio::file_base::flags::read_only;
      break;
    case open_mode::write:
      flags = asio::file_base::flags::write_only;
      break;
    case open_mode::read_write:
      flags = asio::file_base::flags::read_write;
      break;
    default:
      throw std::invalid_argument{"invalid open mode"};
    }
    if (create) {
      flags |= asio::file_base::flags::create;
    }
    if (exclusive) {
      flags |= asio::file_base::flags::exclusive;
    }
    if (truncate) {
      flags |= asio::file_base::flags::truncate;
    }
    if (append) {
      flags |= asio::file_base::flags::append;
    }
    if (sync_all_on_write) {
      flags |= asio::file_base::flags::sync_all_on_write;
    }
    return flags;
  }
};

class file {
public:
  static task<std::expected<file, std::error_code>>
  open(const path &p, const open_options &options) {
    std::error_code ec;
    asio::file_base::flags flags = options.into_asio_flags();

    auto file_ = co_await spawn_blocking([p, flags, &ec] {
      asio::stream_file file_{detail::current_runtime()->get_io_context()};
      // to prevent encoding corrupt in windows
      // TODO: create a native using CreateFileW and pass it to asio obj
      const std::string path_str = p.string();
      file_.open(path_str.c_str(), flags, ec);
      return file_;
    });
    if (ec)
      co_return std::unexpected(ec);
    co_return file{std::move(*file_)};
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   std::byte &>
  auto read_some(R &&buffer) {
    return read_some(std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto read_some(std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio_handle().async_read_some(asio::buffer(buffer),
                                        std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   const std::byte &>
  auto write_some(R &&buffer) {
    return write_some(std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto write_some(std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio_handle().async_write_some(asio::buffer(buffer),
                                         std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   std::byte &>
  auto read(R &&buffer) {
    return read(std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto read(std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio::async_read(asio_handle(), asio::buffer(buffer),
                           std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   const std::byte &>
  auto write(R &&buffer) {
    return write(std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto write(std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, buffer]<typename Args>(Args &&token) {
          asio::async_write(asio_handle(), asio::buffer(buffer),
                            std::forward<Args>(token));
        });
  }

  task<std::expected<uint64_t, std::error_code>> seek(int64_t offset,
                                                      seek_origin origin) {
    std::error_code ec;
    auto pos = co_await spawn_blocking([this, offset, origin, &ec] {
      asio::file_base::seek_basis a_og;
      switch (origin) {
      case seek_origin::begin:
        a_og = asio::file_base::seek_basis::seek_set;
        break;
      case seek_origin::current:
        a_og = asio::file_base::seek_basis::seek_cur;
        break;
      case seek_origin::end:
        a_og = asio::file_base::seek_basis::seek_end;
        break;
      default:
        throw std::invalid_argument{"invalid seek origin"};
      }
      return asio_handle().seek(offset, a_og, ec);
    });
    if (ec)
      co_return std::unexpected(ec);
    co_return pos;
  }

  task<std::expected<uint64_t, std::error_code>> position() {
    return seek(0, seek_origin::current);
  }

  task<std::expected<uint64_t, std::error_code>> size() {
    std::error_code ec;
    auto size =
        co_await spawn_blocking([this, &ec] { return asio_handle().size(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return size;
  }

  task<std::expected<void, std::error_code>> resize(uint64_t size) {
    std::error_code ec;
    auto status = co_await spawn_blocking(
        [this, size, &ec] { asio_handle().resize(size, ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return std::expected<void, std::error_code>{};
  }

  task<std::expected<void, std::error_code>> sync_data() {
    std::error_code ec;
    auto status =
        co_await spawn_blocking([this, &ec] { asio_handle().sync_data(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return std::expected<void, std::error_code>{};
  }

  task<std::expected<void, std::error_code>> sync_all() {
    std::error_code ec;
    auto status =
        co_await spawn_blocking([this, &ec] { asio_handle().sync_all(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return std::expected<void, std::error_code>{};
  }

  bool is_open() { return asio_handle().is_open(); }

  std::expected<void, std::error_code> close() {
    std::error_code ec;
    file_.close(ec);
    if (ec)
      return std::unexpected(ec);
    return {};
  }

  asio::stream_file &asio_handle() noexcept { return file_; }

private:
  explicit file(asio::stream_file file) : file_{std::move(file)} {}

  asio::stream_file file_;
};

class random_access_file {
public:
  static task<std::expected<random_access_file, std::error_code>>
  open(const path &p, const open_options &options) {
    std::error_code ec;
    asio::file_base::flags flags = options.into_asio_flags();

    auto file_ = co_await spawn_blocking([p, flags, &ec] {
      asio::random_access_file file_{
          detail::current_runtime()->get_io_context()};
      // to prevent encoding corrupt in windows
      // TODO: create a native using CreateFileW and pass it to asio obj
      const std::string path_str = p.string();
      file_.open(path_str.c_str(), flags, ec);
      return file_;
    });
    if (ec)
      co_return std::unexpected(ec);
    co_return random_access_file{std::move(*file_)};
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   std::byte &>
  auto read_some_at(uint64_t offset, R &&buffer) {
    return read_some_at(
        offset, std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto read_some_at(uint64_t offset, std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, offset, buffer]<typename Args>(Args &&token) {
          asio_handle().async_read_some_at(offset, asio::buffer(buffer),
                                           std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   const std::byte &>
  auto write_some_at(uint64_t offset, R &&buffer) {
    return write_some_at(
        offset, std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto write_some_at(uint64_t offset, std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, offset, buffer]<typename Args>(Args &&token) {
          asio_handle().async_write_some_at(offset, asio::buffer(buffer),
                                            std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   std::byte &>
  auto read(uint64_t offset, R &&buffer) {
    return read(offset,
                std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto read(uint64_t offset, std::span<std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, offset, buffer]<typename Args>(Args &&token) {
          asio::async_read_at(asio_handle(), offset, asio::buffer(buffer),
                              std::forward<Args>(token));
        });
  }

  template <std::ranges::contiguous_range R>
    requires std::is_convertible_v<std::ranges::range_reference_t<R>,
                                   const std::byte &>
  auto write(uint64_t offset, R &&buffer) {
    return write(offset,
                 std::span{std::ranges::data(buffer), std::size(buffer)});
  }

  auto write(uint64_t offset, std::span<const std::byte> buffer) {
    return detail::async_op<size_t>(
        [this, offset, buffer]<typename Args>(Args &&token) {
          asio::async_write_at(asio_handle(), offset, asio::buffer(buffer),
                               std::forward<Args>(token));
        });
  }

  task<std::expected<uint64_t, std::error_code>> size() {
    std::error_code ec;
    auto size =
        co_await spawn_blocking([this, &ec] { return asio_handle().size(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return size;
  }

  task<std::expected<void, std::error_code>> resize(uint64_t size) {
    std::error_code ec;
    auto status = co_await spawn_blocking(
        [this, size, &ec] { return asio_handle().resize(size, ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return status;
  }

  task<std::expected<void, std::error_code>> sync_data() {
    std::error_code ec;
    auto status = co_await spawn_blocking(
        [this, &ec] { return asio_handle().sync_data(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return status;
  }

  task<std::expected<void, std::error_code>> sync_all() {
    std::error_code ec;
    auto status = co_await spawn_blocking(
        [this, &ec] { return asio_handle().sync_all(ec); });
    if (ec)
      co_return std::unexpected(ec);
    co_return status;
  }

  bool is_open() { return asio_handle().is_open(); }

  std::expected<void, std::error_code> close() {
    std::error_code ec;
    file_.close(ec);
    if (ec)
      return std::unexpected(ec);
    return {};
  }

  asio::random_access_file &asio_handle() noexcept { return file_; }

private:
  explicit random_access_file(asio::random_access_file file)
      : file_{std::move(file)} {}

  asio::random_access_file file_;
};
} // namespace coasio::filesystem

#endif // !COASIO_FILESYSTEM_FILE_HPP