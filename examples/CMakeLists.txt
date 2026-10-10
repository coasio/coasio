add_executable(smoke_test smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE coasio)

add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE coasio)

add_executable(echo_client echo_client.cpp)
target_link_libraries(echo_client PRIVATE coasio)

add_executable(cancel_that cancel_that.cpp)
target_link_libraries(cancel_that PRIVATE coasio)

add_executable(dns_req dns_req.cpp)
target_link_libraries(dns_req PRIVATE coasio)