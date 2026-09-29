#ifndef COASIO_DETAIL_FWD_HPP
#define COASIO_DETAIL_FWD_HPP

namespace coasio {
  class runtime;

  namespace detail {
    // declared in runtime.hpp
    runtime *current_runtime() noexcept;

    void runtime_schedule(runtime *rt, std::coroutine_handle<> h) noexcept;
  }
}

#endif // !COASIO_DETAIL_FWD_HPP