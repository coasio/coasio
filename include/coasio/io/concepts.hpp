#ifndef COASIO_IO_CONCEPTS_HPP
#define COASIO_IO_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <expected>
#include <ranges>
#include <span>
#include <system_error>
#include <type_traits>

#include "coasio/concepts.hpp"

namespace coasio::io {

template <typename T> using io_result = std::expected<T, std::error_code>;

template <typename S>
concept async_read_stream = requires(S &s, std::span<std::byte> buf) {
  { s.read_some(buf) } -> awaitable_of<io_result<std::size_t>>;
};

template <typename S>
concept async_write_stream = requires(S &s, std::span<const std::byte> buf) {
  { s.write_some(buf) } -> awaitable_of<io_result<std::size_t>>;
};

/// A bidirectional stream
template <typename S>
concept async_stream = async_read_stream<S> && async_write_stream<S>;

template <typename B>
concept dynamic_buffer = requires(B &b, const B &cb, std::size_t n) {
  { cb.readable_span() } noexcept -> std::same_as<std::span<const std::byte>>;
  { b.writable_span(n) } -> std::same_as<std::span<std::byte>>;
  { b.commit(n) } noexcept;
  { b.advance(n) } noexcept;
};

template <typename S>
concept async_buf_read_stream =
    async_read_stream<S> && requires(S &s, std::size_t n) {
      { s.fill_buf() } -> awaitable_of<io_result<std::span<const std::byte>>>;
      { s.consume(n) } noexcept;
    };

/// basic buffers
template <typename R>
concept const_buffer_sequence =
    std::ranges::input_range<R> &&
    std::convertible_to<std::ranges::range_value_t<R>,
                        std::span<const std::byte>>;

template <typename R>
concept mutable_buffer_sequence =
    std::ranges::input_range<R> &&
    std::convertible_to<std::ranges::range_value_t<R>, std::span<std::byte>>;

} // namespace coasio::io

#endif // !COASIO_IO_CONCEPTS_HPP