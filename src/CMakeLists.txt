add_library(coasio
        STATIC
        runtime.cpp
)

target_include_directories(coasio
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(coasio PUBLIC asio)

target_compile_features(coasio PUBLIC cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(coasio PUBLIC Threads::Threads)

add_library(coasio::coasio ALIAS coasio)

install(TARGETS coasio
        EXPORT coasioTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
)