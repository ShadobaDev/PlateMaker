cmake_minimum_required(VERSION 3.25)

project(
    platemaker
    VERSION 0.5.0
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
#
# Windows (MSVC *and* MinGW) fetches the same pinned prebuilt "web" libvips — the official minimal
# build (38 DLLs / ~23 MB, no vips-modules dir) that still ships every format Platemaker touches
# (PNG/JPEG/WebP/TIFF). MinGW no longer uses MSYS2's `mingw-w64-x86_64-libvips` (the full build: ~92
# DLLs / ~34 MB), so a MinGW build needs neither the MSYS2 libvips package nor an MSVC build present.
# Linux uses whatever pkg-config finds. This one version string drives the download URL and the SBOM
# entry in lib/CMakeLists.txt, so the fetched and reported versions cannot drift apart.
set(PLATEMAKER_VIPS_WEB_VERSION "8.18.2")

if(WIN32)
    message(STATUS "[Windows] Fetching prebuilt libvips web build v${PLATEMAKER_VIPS_WEB_VERSION} via FetchContent...")

    # Both toolchains fetch the same slim web zip. Each build downloads its own copy into its own build
    # tree, so the branches are independent — a MinGW build never depends on an MSVC build having run.
    FetchContent_Declare(
        vips_binaries
        URL https://github.com/libvips/build-win64-mxe/releases/download/v${PLATEMAKER_VIPS_WEB_VERSION}/vips-dev-x64-web-${PLATEMAKER_VIPS_WEB_VERSION}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(vips_binaries)
    FetchContent_GetProperties(vips_binaries SOURCE_DIR VIPS_EXTRACTED_DIR)
    set(VIPS_ROOT_DIR "${VIPS_EXTRACTED_DIR}")

    # Alias VIPS::vips-cpp so the sub-projects (lib/cli/gui) need no toolchain-specific link section.
    # (The name is historical — we link the C libvips; no C++ vips symbols are ever referenced.)
    if(MSVC)
        # MSVC links the web zip's MS import libs. We reference only the vips C API, so although the
        # zip's libvips-cpp is built against LLVM libc++, no C++ symbols are pulled across the boundary.
        add_library(VIPS::vips-cpp SHARED IMPORTED GLOBAL)
        set_target_properties(VIPS::vips-cpp PROPERTIES
            IMPORTED_IMPLIB   "${VIPS_ROOT_DIR}/lib/libvips-cpp.lib"
            IMPORTED_LOCATION "${VIPS_ROOT_DIR}/bin/libvips-cpp-42.dll"
            INTERFACE_LINK_LIBRARIES "${VIPS_ROOT_DIR}/lib/libvips.lib;${VIPS_ROOT_DIR}/lib/libglib-2.0.lib;${VIPS_ROOT_DIR}/lib/libgobject-2.0.lib"
        )
    else()
        # MinGW: link the *C* libvips only. The web zip's C++ runtime is LLVM libc++, ABI-incompatible
        # with our libstdc++ — but Platemaker uses only the vips C API (<vips/vips.h>), whose ABI is
        # stable across C++ runtimes. MinGW `ld` links directly against the DLLs (it synthesises import
        # stubs from each DLL's export table), so no .lib / .def / dlltool step is needed — verified by
        # a compile-link-run spike against libvips-42.dll.
        add_library(VIPS::vips-cpp INTERFACE IMPORTED GLOBAL)
        target_link_libraries(VIPS::vips-cpp INTERFACE
            "${VIPS_ROOT_DIR}/bin/libvips-42.dll"
            "${VIPS_ROOT_DIR}/bin/libglib-2.0-0.dll"
            "${VIPS_ROOT_DIR}/bin/libgobject-2.0-0.dll")
    endif()

    target_include_directories(VIPS::vips-cpp INTERFACE
        "${VIPS_ROOT_DIR}/include"
        "${VIPS_ROOT_DIR}/include/glib-2.0"
        "${VIPS_ROOT_DIR}/lib/glib-2.0/include"
    )
    message(STATUS "[Windows] libvips ready at: ${VIPS_ROOT_DIR}")

else()
    # Linux (and WSL): native libvips via pkg-config.
    message(STATUS "[GCC] Using system pkg-config to discover libvips...")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

    # Make alias for linker, so the other sub-projects (lib/cli/gui) do not require specific cmake sections
    add_library(VIPS::vips-cpp INTERFACE IMPORTED GLOBAL)
    target_link_libraries(VIPS::vips-cpp INTERFACE PkgConfig::VIPS)
endif()

# nlohmann/json — header-only, header is unpacked by FetchContent (no compilation)
# FIND_PACKAGE first: Linux system install (apt nlohmann-json3-dev) is reused if present.
# The pinned version drives both the fetch URL and the SBOM fallback in lib/CMakeLists.txt (a system
# install reports its own version instead); one source of truth so the two cannot drift apart.
set(PLATEMAKER_JSON_VERSION "3.11.3")
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v${PLATEMAKER_JSON_VERSION}/json.tar.xz
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

# ---------------------------------------------------------------------------
# Tests
#
# Unit tests (GoogleTest — C++):
#   cmake --preset windows-msvc          # configure
#   cmake --build --preset windows-msvc # build everything incl. tests
#   ctest --preset windows-msvc          # run all tests
#   ctest --preset windows-msvc -R WorkspaceSerializer  # run one suite by name regex
#   ctest --preset windows-msvc -V       # verbose output
#
# CLI tests (pytest — Python):
#   Requires: pip install -r tests/cli-tests/requirements.txt
#   CTest runs them automatically as the "cli-tests" test entry.
#   Run manually (faster iteration):
#     python -m pytest tests/cli-tests/ -v \
#       --platemaker-bin=build/windows-msvc/bin/Release/platemaker-cli.exe
#
# Both test types are discovered and run together by CTest.
# Control with cmake option:  -DPLATEMAKER_BUILD_TESTS=OFF  to skip entirely.
# ---------------------------------------------------------------------------
option(PLATEMAKER_BUILD_TESTS "Build unit tests" ON)
if(PLATEMAKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# CPack — produces TWO versioned archives per build, by CPack component group:
#
#   platemaker-dev-<ver>-<sys>-<type>   (group "dev": Development + Runtime)
#       headers + cmake config + import lib + shared lib + bundled DLLs (Windows)
#   platemaker-cli-<ver>-<sys>-<type>   (group "cli": cli)
#       platemaker-cli + a self-contained copy of the runtime (libs + DLLs)
#
# <sys>  = CPACK_SYSTEM_NAME set per configure preset (linux-x86_64 / windows-mingw / windows-msvc)
# <type> = lowercased CMAKE_BUILD_TYPE (release / debug) — so debug and release
#          archives never collide and a "debug" package is genuinely debug.
#
# Run after building, e.g.:  cpack --preset mingw-release
# Or the whole waterfall:    cmake --workflow --preset dist-mingw-release
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME              "platemaker")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR            "ShadobaDev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE.LGPL")

set(CPACK_GENERATOR "ZIP")              # ZIP default; package presets override (TGZ on Linux)
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist")

# Component-based packaging: one archive per group.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set(CPACK_COMPONENT_DEVELOPMENT_GROUP dev)
set(CPACK_COMPONENT_RUNTIME_GROUP     dev)
set(CPACK_COMPONENT_CLI_GROUP         cli)

# Per-group archive names (encode platform + build type).
string(TOLOWER "${CMAKE_BUILD_TYPE}" _pm_build_type)
if(NOT _pm_build_type)
    set(_pm_build_type "release")       # multi-config fallback
endif()
set(CPACK_ARCHIVE_DEV_FILE_NAME "platemaker-dev-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}-${_pm_build_type}")
set(CPACK_ARCHIVE_CLI_FILE_NAME "platemaker-cli-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}-${_pm_build_type}")

# Strip symbols for Release only — a Debug package must keep its symbols.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CPACK_STRIP_FILES OFF)
else()
    set(CPACK_STRIP_FILES ON)
endif()

set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR}" "${PROJECT_NAME}" "ALL" "/"
)

# LICENSE belongs in both archives — library (dev) ships LGPL, CLI ships GPL.
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.LGPL"
    DESTINATION "."
    RENAME "LICENSE"
    COMPONENT Development
)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.GPL"
    DESTINATION "."
    RENAME "LICENSE"
    COMPONENT cli
)

# Linux tarball: ship install.sh so developers can install without CMake.
# configure_file substitutes @PROJECT_VERSION@ / @PROJECT_DESCRIPTION@ at
# configure time; the script itself generates a correct platemaker.pc at
# install time using the actual prefix chosen by the user.
if(UNIX AND NOT APPLE)
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/install.sh.in"
        "${CMAKE_BINARY_DIR}/install.sh"
        @ONLY
    )
    install(PROGRAMS "${CMAKE_BINARY_DIR}/install.sh"
        DESTINATION "."
        COMPONENT Development
    )

    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/install-cli.sh.in"
        "${CMAKE_BINARY_DIR}/install-cli.sh"
        @ONLY
    )
    install(PROGRAMS "${CMAKE_BINARY_DIR}/install-cli.sh"
        DESTINATION "."
        RENAME "install.sh"
        COMPONENT cli
    )

    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/README-cli.md.in"
        "${CMAKE_BINARY_DIR}/README-cli.md"
        @ONLY
    )
    install(FILES "${CMAKE_BINARY_DIR}/README-cli.md"
        DESTINATION "."
        RENAME "README.md"
        COMPONENT cli
    )
endif()

# Emit a "<archive>.sha256" sidecar (sha256sum format) next to every packaged archive, for release
# verification. Runs as a CPack post-build step, so `cpack --preset …` and the dist workflows produce
# the checksums automatically alongside the dev and cli archives.
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/cmake/cpack_checksums.cmake")

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
