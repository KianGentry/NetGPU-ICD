cmake_minimum_required(VERSION 3.16)
project(NetGPU-ICD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# hidden by default so the icd never leaks vulkan symbols
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

add_compile_options(-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic)

enable_testing()

# wire protocol shared by client and host
add_library(netgpu_common STATIC common/src/wire.cpp common/src/transport.cpp
    common/include/netgpu/transport.hpp)
target_include_directories(netgpu_common PUBLIC common/include)

add_executable(wire_test tests/wire_test.cpp)
target_link_libraries(wire_test PRIVATE netgpu_common)
add_test(NAME wire_test COMMAND wire_test)

find_package(Threads REQUIRED)

add_library(netgpu_transport_unix STATIC transport/unix_domain_socket.cpp
    transport/include/netgpu/unix_domain_socket.hpp)
target_include_directories(netgpu_transport_unix PUBLIC common/include transport/include)
target_link_libraries(netgpu_transport_unix PUBLIC netgpu_common Threads::Threads)

add_executable(transport_test tests/transport_test.cpp)
target_link_libraries(transport_test PRIVATE netgpu_transport_unix)
add_test(NAME transport_test COMMAND transport_test)
set(NETGPU_TRANSPORT_TEST_TIMEOUT_SECONDS 20)
set_tests_properties(transport_test PROPERTIES TIMEOUT ${NETGPU_TRANSPORT_TEST_TIMEOUT_SECONDS})
