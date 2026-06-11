cmake_minimum_required(VERSION 3.21)

project(
    platemaker
    VERSION 0.1.1
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
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# libvips  — image processing engine (LGPL 2.1)
if(MSVC)
    message(STATUS "[MSVC] Automating libvips deployment via FetchContent...")

    # Select libvips for download
    # Version 8.16.0
    FetchContent_Declare(
        vips_binaries
        URL https://github.com/libvips/build-win64-mxe/releases/download/v8.18.2/vips-dev-x64-all-8.18.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    
    # Download libvips and unpack directly to build/
    FetchContent_MakeAvailable(vips_binaries)
    
    # Get path to binaries where CMake automatically unpacked them
    FetchContent_GetProperties(vips_binaries SOURCE_DIR VIPS_EXTRACTED_DIR)
    
    # Register includes
    # include_directories(SYSTEM "${VIPS_EXTRACTED_DIR}/include")
    set(VIPS_ROOT_DIR "${VIPS_EXTRACTED_DIR}")

    # Make alias for linker, so the other sub-projects (lib/cli/gui) do not require specific cmake sections
    add_library(VIPS::vips-cpp SHARED  IMPORTED GLOBAL)
    set_target_properties(VIPS::vips-cpp PROPERTIES
        IMPORTED_IMPLIB   "${VIPS_ROOT_DIR}/lib/libvips-cpp.lib"
        IMPORTED_LOCATION "${VIPS_ROOT_DIR}/bin/libvips-cpp-42.dll" # <--- Wskaż faktyczną nazwę pliku .dll w folderze bin (np. libvips-cpp-42.dll lub libvips-cpp.dll)
        
        # Collect vips-cpp alltogheter with glib and basic vips, so linker sees all of the functions
        INTERFACE_LINK_LIBRARIES "${VIPS_ROOT_DIR}/lib/libvips.lib;${VIPS_ROOT_DIR}/lib/libglib-2.0.lib;${VIPS_ROOT_DIR}/lib/libgobject-2.0.lib"
    )
    
    target_include_directories(VIPS::vips-cpp INTERFACE 
        "${VIPS_ROOT_DIR}/include"
        "${VIPS_ROOT_DIR}/include/glib-2.0"
        "${VIPS_ROOT_DIR}/lib/glib-2.0/include"
    )
    message(STATUS "[MSVC] libvips extracted and linked from: ${VIPS_EXTRACTED_DIR}")

else()
    # For Linux (WSL) and MinGW (MSYS2) keep vips native and let the pkg-config do the job
    message(STATUS "[GCC] Using system pkg-config to discover libvips...")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)
    
    # Make alias for linker, so the other sub-projects (lib/cli/gui) do not require specific cmake sections
    add_library(VIPS::vips-cpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(VIPS::vips-cpp INTERFACE PkgConfig::VIPS)
endif()

# nlohmann/json — header-only, header is unpacked by FetchContent (no compilation)
# FIND_PACKAGE first: Linux system install (apt nlohmann-json3-dev) is reused if present.
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# GoogleTest — only when PLATEMAKER_BUILD_TESTS is enabled (declared below with option())
# The actual FetchContent_Declare lives in tests/lib-unit-tests/CMakeLists.txt so that
# gtest is only fetched/built when the tests subdirectory is actually added.


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

# Unit tests
option(PLATEMAKER_BUILD_TESTS "Build unit tests" ON)
if(PLATEMAKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# CPack — produces versioned dev-package archives
#
# Run after building:  cpack --preset windows-msvc
# Produces:  platemaker-dev-0.1.1-windows-msvc.zip  (or .tar.gz on Linux)
#
# CPack reuses the cmake --install rules defined in lib/CMakeLists.txt.
# The CPACK_INSTALL_CMAKE_PROJECTS list selects only the library targets
# (no tests, no CLI) — adjust if the CLI should ship in the same package.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME              "platemaker-dev")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR            "ShadobaDev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE.LGPL")

# Archive names:  platemaker-dev-0.1.1-<platform>.<ext>
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_GENERATOR "ZIP")              # ZIP on all platforms; override per-preset if needed
set(CPACK_STRIP_FILES ON)               # strip debug symbols from release packages

# Install only the library target (not CLI, not tests)
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR}" "${PROJECT_NAME}" "ALL" "/"
)

include(CPack)

# ---------------------------------------------------------------------------
# Summary printout (visible during cmake configure step)
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "══════════════════════════════════════════")
message(STATUS "  Platemaker ${PROJECT_VERSION}")
message(STATUS "──────────────────────────────────────────")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests   : ${PLATEMAKER_BUILD_TESTS}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "══════════════════════════════════════════")
message(STATUS "")
