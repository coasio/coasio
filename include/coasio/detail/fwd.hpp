#ifndef COASIO_DETAIL_FWD_HPP
#define COASIO_DETAIL_FWD_HPP

#include <coroutine>

namespace coasio {
class worker;
class runtime;

namespace detail {
// declared in runtime.hpp
runtime *current_runtime() noexcept;

worker *current_worker() noexcept;

void runtime_schedule(runtime *rt, std::coroutine_handle<> h);

void runtime_schedule_in(runtime *rt, worker *w, std::coroutine_handle<> h);

} // namespace detail
} // namespace coasio

#endif // !COASIO_DETAIL_FWD_HPP
