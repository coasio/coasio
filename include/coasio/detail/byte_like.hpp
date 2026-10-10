#ifndef COASIO_DETAIL_BYTE_LIKE_HPP
#define COASIO_DETAIL_BYTE_LIKE_HPP

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace coasio::detail {
template <typename T>
concept byte_like = std::same_as<std::remove_cvref_t<T>, char> ||
                    std::same_as<std::remove_cvref_t<T>, signed char> ||
                    std::same_as<std::remove_cvref_t<T>, unsigned char> ||
                    std::same_as<std::remove_cvref_t<T>, std::byte>;
} // namespace coasio::detail

#endif // !COASIO_DETAIL_BYTE_LIKE_HPP