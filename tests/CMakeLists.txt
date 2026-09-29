include(FetchContent)
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

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
