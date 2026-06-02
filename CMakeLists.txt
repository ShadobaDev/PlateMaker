cmake_minimum_required(VERSION 3.21)

project(
    platemaker
    VERSION 0.1.0
    DESCRIPTION "Comic artist canvas tool — pre-processing and post-processing for Webtoon-style publishing"
    HOMEPAGE_URL "https://github.com/ShadobaDev/Platemaker"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Global output directories (keeps binaries out of source tree)
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Portable runtime search paths for Linux executables
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    # Keep build tree binaries runnable from build/ by embedding relative RPATH.
    set(CMAKE_BUILD_RPATH "$ORIGIN:$ORIGIN/../lib")
    set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

    # When installed or packaged, search next to the executable and in ../lib.
    set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
endif()

# ---------------------------------------------------------------------------
# vcpkg toolchain is injected via CMakePresets.json — nothing to do here.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Dependencies (found via vcpkg / system)
# ---------------------------------------------------------------------------

# libvips  — image processing engine (LGPL 2.1)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

# nlohmann/json — header-only JSON (MIT)
find_package(nlohmann_json CONFIG REQUIRED)

# Qt 6 — GUI layer only; guarded by the PLATEMAKER_BUILD_GUI option below
# (Qt is NOT linked into libplatemaker or the CLI binary)
option(PLATEMAKER_BUILD_GUI "Build the Qt 6 GUI binary (platemaker-gui)" ON)
if(PLATEMAKER_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
    # qt_standard_project_setup() was added in Qt 6.3 — set the same options manually
    # so the project builds with Qt 6.2 (Ubuntu 22.04 system package) and newer.
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

# ---------------------------------------------------------------------------
# Compile-time feature flags propagated to all targets
# ---------------------------------------------------------------------------
# When building without Qt the thumbnail cache falls back to synchronous mode.
if(NOT PLATEMAKER_BUILD_GUI)
    add_compile_definitions(PLATEMAKER_NO_QT)
endif()

# ---------------------------------------------------------------------------
# Compiler warnings — sensible cross-platform baseline
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# Sub-projects (order matters: lib first, then consumers)
# ---------------------------------------------------------------------------

# Core + Infrastructure — pure C++, zero Qt dependency
add_subdirectory(lib)

# CLI binary — links libplatemaker, no Qt
add_subdirectory(cli)

# Qt GUI binary — links libplatemaker + Qt 6
if(PLATEMAKER_BUILD_GUI)
    add_subdirectory(gui)
endif()

# Unit tests
option(PLATEMAKER_BUILD_TESTS "Build unit tests" ON)
if(PLATEMAKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary printout (visible during cmake configure step)
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "══════════════════════════════════════════")
message(STATUS "  Platemaker ${PROJECT_VERSION}")
message(STATUS "──────────────────────────────────────────")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build GUI     : ${PLATEMAKER_BUILD_GUI}")
message(STATUS "  Build tests   : ${PLATEMAKER_BUILD_TESTS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "══════════════════════════════════════════")
message(STATUS "")
