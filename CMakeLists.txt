cmake_minimum_required(VERSION 3.14)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older FetchContent deps")
endif()
project(proxy_ptr LANGUAGES CXX)

# Header-only interface library
add_library(proxy_ptr INTERFACE)
target_include_directories(proxy_ptr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(proxy_ptr INTERFACE cxx_std_17)

# Tests
option(PROXY_PTR_BUILD_TESTS "Build tests" ON)
if(PROXY_PTR_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
    )
    FetchContent_MakeAvailable(doctest)

    enable_testing()
    add_executable(proxy_ptr_tests test/test.cpp test/test_atomic.cpp)
    target_link_libraries(proxy_ptr_tests PRIVATE proxy_ptr doctest_with_main)
    target_compile_features(proxy_ptr_tests PRIVATE cxx_std_17)

    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(proxy_ptr_tests)

    # Benchmark (not part of ctest, run manually)
    add_executable(proxy_ptr_bench test/bench.cpp)
    target_link_libraries(proxy_ptr_bench PRIVATE proxy_ptr)
    target_compile_features(proxy_ptr_bench PRIVATE cxx_std_17)
endif()
