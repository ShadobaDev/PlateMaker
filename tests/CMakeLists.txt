cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Test suites
#
# lib-unit-tests  — GoogleTest-based C++ unit tests for libplatemaker.
# lib-fault-tests — its error paths, reached by making libvips/libarchive/libc
#                   calls fail (Linux only: ELF symbol interposition).
# cli-tests       — pytest-based Python integration tests for the CLI binary.
#
# All are registered with CTest and run via `ctest` or
# `cmake --build --preset <preset> --target test`.
# ---------------------------------------------------------------------------

add_subdirectory(lib-unit-tests)
if(UNIX AND NOT APPLE)
    add_subdirectory(lib-fault-tests)
endif()
add_subdirectory(cli-tests)
