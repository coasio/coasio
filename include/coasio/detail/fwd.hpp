#ifndef COASIO_DETAIL_FWD_HPP
#define COASIO_DETAIL_FWD_HPP

#include <coroutine>

namespace coasio {
class runtime;

namespace detail {
// declared in runtime.hpp
runtime *current_runtime() noexcept;

void runtime_schedule(runtime *rt, std::coroutine_handle<> h) noexcept;
} // namespace detail
} // namespace coasio

#endif // !COASIO_DETAIL_FWD_HPP
