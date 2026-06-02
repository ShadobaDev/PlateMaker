cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# pytest-based integration tests for the platemaker CLI binary.
#
# Using Python/pytest (rather than bash/PowerShell) keeps the test suite
# cross-platform: the same tests run unchanged on Linux and Windows.
#
# Requirements (both platforms):
#   pip install pytest   (or: pip install -r tests/cli-tests/requirements.txt)
# ---------------------------------------------------------------------------

find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Python3_FOUND)
    message(WARNING
        "Python 3 interpreter not found — CLI tests will be skipped.\n"
        "  Install Python 3 and make sure it is on PATH."
    )
    return()
endif()

# Check that pytest module is importable.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pytest --version
    RESULT_VARIABLE _pytest_check
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT _pytest_check EQUAL 0)
    message(WARNING
        "pytest not found — CLI tests will be skipped.\n"
        "  Run: pip install pytest   (or: pip install -r tests/cli-tests/requirements.txt)"
    )
    return()
endif()

# ---------------------------------------------------------------------------
# CTest entry — passes the CLI binary path to pytest via --platemaker-bin.
#
# Generator expression $<TARGET_FILE:platemaker> is resolved at test time
# (supported in add_test COMMAND since CMake 3.21).
# ---------------------------------------------------------------------------
add_test(
    NAME    cli-tests
    COMMAND "${Python3_EXECUTABLE}" -m pytest
                "${CMAKE_CURRENT_SOURCE_DIR}"
                -v
                --tb=short
                "--platemaker-bin=$<TARGET_FILE:platemaker>"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_tests_properties(cli-tests PROPERTIES
    LABELS "cli;integration"
    TIMEOUT 120
)
