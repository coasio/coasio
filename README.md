# CoAsio

[![Windows MSVC](https://github.com/ttheghost/coasio/actions/workflows/ci-msvc.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-msvc.yml)
[![Linux GCC](https://github.com/ttheghost/coasio/actions/workflows/ci-gcc.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-gcc.yml)
<!--[![Linux Clang](https://github.com/ttheghost/coasio/actions/workflows/ci-clang.yml/badge.svg)](https://github.com/ttheghost/coasio/actions/workflows/ci-clang.yml)-->

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