#ifndef COASIO_FILESYSTEM_HPP
#define COASIO_FILESYSTEM_HPP

#include <expected>
#include <filesystem>

#include "coasio.hpp"
#include "coasio/task.hpp"

namespace coasio::filesystem {
using path = std::filesystem::path;
using file_status = std::filesystem::file_status;
using file_type = std::filesystem::file_type;
using perms = std::filesystem::perms;
using perm_options = std::filesystem::perm_options;
using copy_options = std::filesystem::copy_options;
using directory_options = std::filesystem::directory_options;
using space_info = std::filesystem::space_info;
using file_time_type = std::filesystem::file_time_type;

enum class seek_origin { begin, current, end };

[[nodiscard]] inline task<std::expected<uintmax_t, std::error_code>>
file_size(const path &path_) {
  std::error_code ec;
  auto size = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::file_size(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return size;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
exists(const path &path_) {
  std::error_code ec;
  auto exists = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::exists(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return exists;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
is_regular_file(const path &path_) {
  std::error_code ec;
  auto is_regular_file = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::is_regular_file(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return is_regular_file;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
is_directory(const path &path_) {
  std::error_code ec;
  auto is_directory = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::is_directory(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return is_directory;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
is_symlink(const path &path_) {
  std::error_code ec;
  auto is_symlink = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::is_symlink(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return is_symlink;
}

[[nodiscard]] inline task<std::expected<file_status, std::error_code>>
status(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::status(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<file_status, std::error_code>>
symlink_status(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::symlink_status(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<uintmax_t, std::error_code>>
hard_link_count(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::hard_link_count(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
equivalent(const path &path1_, const path &path2_) {
  std::error_code ec;
  auto status = co_await spawn_blocking([path1_, path2_, &ec] {
    return std::filesystem::equivalent(path1_, path2_, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<file_time_type, std::error_code>>
last_write_time(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::last_write_time(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
last_write_time(const path &path_, const file_time_type ftt) {
  std::error_code ec;
  auto status = co_await spawn_blocking([path_, ftt, &ec] {
    return std::filesystem::last_write_time(path_, ftt, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
create_directory(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::create_directory(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
create_directories(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::create_directories(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
remove(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::remove(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<uintmax_t, std::error_code>>
remove_all(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::remove_all(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
rename(const path &from, const path &to) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [from, to, &ec] { return std::filesystem::rename(from, to, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
copy(const path &from, const path &to, copy_options options = {}) {
  std::error_code ec;
  auto status = co_await spawn_blocking([from, to, options, &ec] {
    return std::filesystem::copy(from, to, options, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<bool, std::error_code>>
copy_file(const path &from, const path &to, copy_options options = {}) {
  std::error_code ec;
  auto status = co_await spawn_blocking([from, to, options, &ec] {
    return std::filesystem::copy_file(from, to, options, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
copy_symlink(const path &from, const path &to) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [from, to, &ec] { return std::filesystem::copy_symlink(from, to, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
create_hard_link(const path &existing, const path &new_link) {
  std::error_code ec;
  auto status = co_await spawn_blocking([existing, new_link, &ec] {
    return std::filesystem::create_hard_link(existing, new_link, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
create_symlink(const path &target, const path &link) {
  std::error_code ec;
  auto status = co_await spawn_blocking([target, link, &ec] {
    return std::filesystem::create_symlink(target, link, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
create_directory_symlink(const path &target, const path &link) {
  std::error_code ec;
  auto status = co_await spawn_blocking([target, link, &ec] {
    return std::filesystem::create_directory_symlink(target, link, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
read_symlink(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::read_symlink(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
absolute(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::absolute(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
canonical(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::canonical(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
weakly_canonical(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::weakly_canonical(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
relative(const path &path_, const path &base) {
  std::error_code ec;
  auto status = co_await spawn_blocking([path_, base, &ec] {
    return std::filesystem::relative(path_, base, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
proximate(const path &path_, const path &base) {
  std::error_code ec;
  auto status = co_await spawn_blocking([path_, base, &ec] {
    return std::filesystem::proximate(path_, base, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

// lexically_normal(), lexically_relative(), etc. remain synchronous because
// they are purely lexical operations on a path.

[[nodiscard]] inline task<std::expected<path, std::error_code>> current_path() {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [&ec] { return std::filesystem::current_path(ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
current_path(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::current_path(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<path, std::error_code>>
temp_directory_path() {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [&ec] { return std::filesystem::temp_directory_path(ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<space_info, std::error_code>>
space(const path &path_) {
  std::error_code ec;
  auto status = co_await spawn_blocking(
      [path_, &ec] { return std::filesystem::space(path_, ec); });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

[[nodiscard]] inline task<std::expected<void, std::error_code>>
permissions(const path &path_, const perms perms_,
            const perm_options options = perm_options::replace) {
  std::error_code ec;
  auto status = co_await spawn_blocking([path_, perms_, options, &ec] {
    return std::filesystem::permissions(path_, perms_, options, ec);
  });
  if (ec)
    co_return std::unexpected(ec);
  co_return status;
}

} // namespace coasio::filesystem

#endif // !COASIO_FILESYSTEM_HPP