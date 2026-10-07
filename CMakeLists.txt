# 3.20 for CMAKE_CXX_STANDARD 23
cmake_minimum_required(VERSION 3.20...3.31)
project(coasio VERSION 0.0.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COASIO_BUILD_EXAMPLES "Build coasio examples" ${PROJECT_IS_TOP_LEVEL})
option(COASIO_BUILD_TESTS "Build coasio test suite" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-38-2
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)

if (NOT TARGET asio)
    add_library(asio INTERFACE)
    target_include_directories(asio
            INTERFACE
            $<BUILD_INTERFACE:${asio_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

    install(TARGETS asio
            EXPORT coasioTargets
    )

    install(DIRECTORY "${asio_SOURCE_DIR}/include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif ()

add_subdirectory(src)

if (COASIO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

if (COASIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

set(COASIO_CMAKECONFIG_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/coasio")

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/coasioConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/coasioConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/coasioConfig.cmake"
        INSTALL_DESTINATION "${COASIO_CMAKECONFIG_DIR}"
)

install(EXPORT coasioTargets
        FILE coasioTargets.cmake
        NAMESPACE coasio::
        DESTINATION "${COASIO_CMAKECONFIG_DIR}"
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/coasioConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/coasioConfigVersion.cmake"
        DESTINATION "${COASIO_CMAKECONFIG_DIR}"
)