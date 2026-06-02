cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# GoogleTest-based unit tests for libplatemaker (platemaker-lib target).
#
# System packages (Linux):  sudo apt install libgtest-dev
# vcpkg (Windows):          declared in vcpkg.json as "gtest"
# ---------------------------------------------------------------------------

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(WARNING
        "GoogleTest not found — lib-unit-tests will be skipped.\n"
        "  Linux : sudo apt install libgtest-dev\n"
        "  vcpkg : included in vcpkg.json; run cmake with the vcpkg preset"
    )
    return()
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
add_executable(platemaker-lib-tests
    test_pixel_buffer.cpp
    test_scaled_strip.cpp
    test_workspace_serializer.cpp
)

target_link_libraries(platemaker-lib-tests
    PRIVATE
        Platemaker::platemaker
        GTest::gtest
        GTest::gtest_main
)

# Inherit include paths from the library so test files can use
# #include <platemaker/...> without extra configuration.

# ---------------------------------------------------------------------------
# CTest integration — discovers individual TEST() cases automatically
# ---------------------------------------------------------------------------
include(GoogleTest)
gtest_discover_tests(platemaker-lib-tests
    DISCOVERY_TIMEOUT 30
)
