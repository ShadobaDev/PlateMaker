cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# GoogleTest-based unit tests for libplatemaker (platemaker-lib target).
#
# GTest is discovered in this order:
#   1. System install  (Linux: apt install libgtest-dev)
#   2. FetchContent    (Windows / any platform without a system install)
# ---------------------------------------------------------------------------

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    # MSVC: link the shared CRT (/MD) to match our build.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # We only use gtest/gtest_main — skip gmock to avoid its include-path
    # issues when building with MSVC multi-config.
    set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        GTest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(GTest)
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
add_executable(platemaker-lib-tests
    test_pixel_buffer.cpp
    test_scaled_strip.cpp
    test_workspace_serializer.cpp
    test_canvas_profile_matcher.cpp
    test_canvas_staleness.cpp
    test_profile_ids.cpp
    test_output_profile_presets.cpp
    test_path_encoding.cpp
    test_build_info.cpp
)

# libvips is PRIVATE on platemaker-lib (VipsImage never appears in the public API),
# but tests are part of this project rather than consumers: they need VIPS_INIT and
# vips_black() to build synthetic images for the ScaledStrip memory-contract test.
target_link_libraries(platemaker-lib-tests
    PRIVATE
        Platemaker::platemaker
        VIPS::vips-cpp
        GTest::gtest
        GTest::gtest_main
)

# ---------------------------------------------------------------------------
# CTest integration — discovers individual TEST() cases automatically
# ---------------------------------------------------------------------------
include(GoogleTest)
gtest_discover_tests(platemaker-lib-tests
    DISCOVERY_TIMEOUT 30
)
