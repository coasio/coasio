# 3.20 for CMAKE_CXX_STANDARD 23
cmake_minimum_required(VERSION 3.20...3.31)
project(coasio VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COASIO_BUILD_TESTS "Build coasio test suite" ON)

include(FetchContent)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-38-2
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

add_subdirectory(src)
add_subdirectory(examples)

if(COASIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()