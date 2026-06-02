cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Test suites
#
# lib-unit-tests — GoogleTest-based C++ unit tests for libplatemaker.
# cli-tests      — pytest-based Python integration tests for the CLI binary.
#
# Both suites are registered with CTest and run via `ctest` or
# `cmake --build --preset <preset> --target test`.
# ---------------------------------------------------------------------------

add_subdirectory(lib-unit-tests)
add_subdirectory(cli-tests)
