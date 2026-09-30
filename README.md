# CoAsio

[![Windows MSVC](https://github.com/ttheghost/coasio/actions/workflows/ci-msvc.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-msvc.yml)
[![Linux GCC](https://github.com/ttheghost/coasio/actions/workflows/ci-gcc.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-gcc.yml)
[![Linux Clang](https://github.com/ttheghost/coasio/actions/workflows/ci-clang.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-clang.yml)

A C++23 coroutine runtime built on top of Asio.

## Minimum Environment Requirements

- CMake 3.24 or later
- C++ Standard: C++23 (-std=c++23)
- GCC: 12.1+
- Clang: 17+ (Relying on libstdc++ 12+ for the standard library). // Clang's libc++ still doesn't fully support `std::move_only_function`.
- MSVC: 19.33+ (Visual Studio 2022 v17.3+).
