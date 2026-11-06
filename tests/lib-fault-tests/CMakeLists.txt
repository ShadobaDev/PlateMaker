cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Fault-injection tests for libplatemaker: the library's calls into libvips,
# libarchive and libc made to fail on demand (fault.hpp), to reach the error
# paths no ordinary input does. ELF only — they rely on the executable's own
# definitions taking the library's calls — so only on Linux, where coverage
# is measured.
# ---------------------------------------------------------------------------

# lib-unit-tests found or fetched GTest; a found one's targets are visible in its directory only.
if(NOT TARGET GTest::gtest)
    find_package(GTest REQUIRED)
endif()

add_executable(platemaker-lib-fault-tests
    fault.cpp
    wrappers.cpp
    test_fault_harness.cpp
    test_libvips_failures.cpp
    test_file_failures.cpp
)

# Exported, so libplatemaker.so binds to the wrappers and not to the libraries behind them.
set_target_properties(platemaker-lib-fault-tests PROPERTIES ENABLE_EXPORTS ON)

target_link_libraries(platemaker-lib-fault-tests
    PRIVATE
        Platemaker::platemaker
        VIPS::vips-cpp
        LibArchive::LibArchive
        ${CMAKE_DL_LIBS}
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(platemaker-lib-fault-tests
    DISCOVERY_TIMEOUT 30
)
