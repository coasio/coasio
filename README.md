# CoAsio

[![Windows MSVC](https://github.com/coasio/coasio/actions/workflows/ci-msvc.yml/badge.svg)](https://github.com/coasio/coasio/actions/workflows/ci-msvc.yml)
[![Linux GCC](https://github.com/coasio/coasio/actions/workflows/ci-gcc.yml/badge.svg)](https://github.com/coasio/coasio/actions/workflows/ci-gcc.yml)
<!--[![Linux Clang](https://github.com/coasio/coasio/actions/workflows/ci-clang.yml/badge.svg)](https://github.com/coasio/coasio/actions/workflows/ci-clang.yml)-->

A C++23 coroutine runtime built on top of Asio.

## Minimum Environment Requirements

- CMake 3.20 or later
- C++ Standard: C++23 (-std=c++23)
- C++ thread support
- CoAsio requires standalone Asio 1.38.2. The dependency is automatically fetched by CMake.

### Officially tested configurations

- Linux x86-64
  - GCC 12 / libstdc++
  - Clang / libc++ **is not currently considered supported**.
- Windows x86-64
  - MSVC 19.33+ / Visual Studio 2022

> ### Critical: Coroutine Lambdas and Lifetime of Captures
>
> In C++20, **never use lambda captures with temporary coroutine lambdas**.
>
> When a lambda returns a `task<T>`, the lambda object's member variables store its captures. If the lambda is a temporary (e.g., passed directly into `spawn(...)`), the lambda closure is destroyed at the end of the full-expression, leaving the coroutine frame with a **dangling `this` pointer**. Resuming the coroutine after suspension will cause **undefined behavior or a `SIGSEGV`**, even when capturing by value (`std::shared_ptr`).
>
> #### Dangerous (Causes Undefined Behavior / Segfault):
> ```cpp
> auto state = std::make_shared<MyState>();
>
> rt.spawn([state]() -> coasio::task<void> {
>     co_await something();
>     state->do_work(); // Use after free / SIGSEGV
> });
> ```
>
> ####  Safe Alternatives:
>
> **1. Pass state as function arguments (Recommended):**
> Coroutine arguments are copied/moved directly into the heap-allocated coroutine frame, keeping them alive for the coroutine's entire lifetime:
> ```cpp
> rt.spawn([](auto state) -> coasio::task<void> {
>     co_await something();
>     state->do_work(); //  Safe: 'state' lives inside the coroutine frame
> }, state);
> ```
>
> **2. Use an explicit object parameter (C++23 Deduced `this`):**
> Passing `this auto self` by value copies the entire closure object into the coroutine frame:
> ```cpp
> rt.spawn([state](this auto self) -> coasio::task<void> {
>     co_await something();
>     self.state->do_work(); //  Safe: closure copied into frame
> });
> ```