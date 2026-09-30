include(FetchContent)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    TRUE
)
# temporarily downgrade the standard only for Catch2 while it compiles
# it fails to compile in C++23, Clang 17 (paired with GCC 14's libstdc++)
set(CMAKE_CXX_STANDARD_OLD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD 20)
FetchContent_MakeAvailable(Catch2)
set(CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD_OLD})

function(coasio_add_test TEST_NAME)
    set(TARGET_NAME "test_${TEST_NAME}")
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${TARGET_NAME} PRIVATE coasio Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(${TARGET_NAME} PROPERTIES TIMEOUT 30)
endfunction()

coasio_add_test(rt_basic runtime_basic.cpp)
coasio_add_test(sync_oneshot sync/oneshot.cpp)
coasio_add_test(sync_semaphore sync/semaphore.cpp)
