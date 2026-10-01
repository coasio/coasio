add_library(coasio
        STATIC
        runtime.cpp
)

target_include_directories(coasio
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(coasio
        PUBLIC
        asio
)

target_compile_features(coasio PUBLIC cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(coasio PUBLIC Threads::Threads)