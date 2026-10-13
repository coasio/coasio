#ifndef COASIO_DETAIL_UNCANCELLABLE_HPP
#define COASIO_DETAIL_UNCANCELLABLE_HPP

#include <type_traits>

namespace coasio::detail {

template <typename Awaitable> struct uncancellable_awaitable {
  Awaitable awaitable;
};

template <typename Awaitable> auto uncancellable(Awaitable &&awaitable) {
  return uncancellable_awaitable<std::decay_t<Awaitable>>{
      std::forward<Awaitable>(awaitable)};
}

} // namespace coasio::detail

#endif // !COASIO_DETAIL_UNCANCELLABLE_HPP