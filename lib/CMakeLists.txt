cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(PLATEMAKER_SOURCES
    # Models (non-trivial computed properties)
    src/models/canvas_profile.cpp
    src/models/project_item.cpp

    # Infrastructure — control
    src/infrastructure/control/cancellation_token.cpp
    src/infrastructure/id_generator/id_generator.cpp
    src/infrastructure/log/log.cpp

    # Core pipeline
    src/core/pixel_buffer/pixel_buffer.cpp
    src/core/scaler/scaler.cpp
    src/core/scaled_strip/scaled_strip.cpp
    src/core/margin_cropper/margin_cropper.cpp
    src/core/template_generator/template_generator.cpp
    src/core/canvas_profile_matcher/canvas_profile_matcher.cpp
    src/core/processing_pipeline/processing_pipeline.cpp

    # Infrastructure / IO
    src/infrastructure/file/file_meta_data.cpp
    src/infrastructure/image_io/image_io.cpp
    src/infrastructure/model_json/model_json.cpp
    src/infrastructure/workspace_serializer/workspace_serializer.cpp
    src/infrastructure/workspace_editor/workspace_editor.cpp
    src/infrastructure/project_editor/project_editor.cpp
    src/infrastructure/thumbnail_cache/thumbnail_cache.cpp
    src/infrastructure/build_info/build_info.cpp
)

# ---------------------------------------------------------------------------
# Library target
#
# SHARED is required by the LGPLv2 licence: end users must be able to
# relink the library.  The CMake target is named "platemaker-lib" to avoid
# clashing with the "platemaker" CLI executable target.  OUTPUT_NAME is set
# to "platemaker" so the on-disk artefact is:
#   Linux/macOS  →  libplatemaker.so  /  libplatemaker.dylib
#   Windows      →  platemaker.dll   +  platemaker.lib  (import library)
# ---------------------------------------------------------------------------
add_library(platemaker-lib SHARED ${PLATEMAKER_SOURCES})

set_target_properties(platemaker-lib PROPERTIES
    OUTPUT_NAME platemaker
    # EXPORT_NAME controls the target name in the installed cmake package:
    # Platemaker:: + platemaker = Platemaker::platemaker (matches the build-tree alias).
    EXPORT_NAME platemaker
    # Linux/macOS: creates libplatemaker.so.1 symlink (SOVERSION) and
    # libplatemaker.so.0.1.1 (VERSION).  Windows ignores VERSION for the DLL
    # filename but embeds it in the PE import-lib metadata.
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Windows: embed version metadata (Explorer "Details" tab, Process Explorer) into libplatemaker.dll,
# generated from PROJECT_VERSION so it stays in sync. MinGW compiles the .rc via windres; enable the RC
# language explicitly since the project declares only CXX.
if(WIN32)
    enable_language(RC)
    configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/platemaker-version.rc" @ONLY)
    target_sources(platemaker-lib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/platemaker-version.rc")
endif()

# Namespaced alias — consumers use Platemaker::platemaker in target_link_libraries
add_library(Platemaker::platemaker ALIAS platemaker-lib)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(platemaker-lib
    PUBLIC
        # Consumers include as: #include <platemaker/...>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Generated platemaker_export.h lives in the build tree (platemaker/ subdir)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Dependencies
#
# PRIVATE: neither VipsImage nor nlohmann types appear in the public API headers,
# so consumers of the installed package do not need these headers or import libs.
# ---------------------------------------------------------------------------
target_link_libraries(platemaker-lib
    PRIVATE
        VIPS::vips-cpp
        nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# Compiler features
# ---------------------------------------------------------------------------
target_compile_features(platemaker-lib PUBLIC cxx_std_20)

# MSVC: suppress C4251 (STL members in __declspec(dllexport) classes).
# This warning fires for every std::string / std::vector member in an exported
# type, which is expected for a header-only public API compiled by the same
# toolchain.  PUBLIC so consumers building against our headers are also quiet.
if(MSVC)
    target_compile_options(platemaker-lib PUBLIC /wd4251)
endif()

# ---------------------------------------------------------------------------
# Windows DLL export helper
#
# Generating a proper export header avoids the need to annotate every symbol
# manually with __declspec(dllexport/dllimport).
# ---------------------------------------------------------------------------
include(GenerateExportHeader)
generate_export_header(platemaker-lib
    BASE_NAME PLATEMAKER
    EXPORT_FILE_NAME
        "${CMAKE_CURRENT_BINARY_DIR}/platemaker/platemaker_export.h"
)

# ---------------------------------------------------------------------------
# Version / metadata header — generated from the root project() declaration.
# Header-only constexpr values (single source of truth); consumed by the CLI
# (--version) and available to any package consumer via #include <platemaker/version.hpp>.
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/platemaker/version.hpp"
    @ONLY
)

# Build-tree-only version marker (NOT installed, NOT public API): a single "@(#)…" string literal
# embedded in the library and CLI binaries so the version is discoverable via `what` / `strings` on
# platforms with no Windows VERSIONINFO resource (Linux/macOS). See cmake/ident.hpp.in. Resolvable as
# <platemaker/ident.hpp> from both the lib and CLI via the target's BUILD_INTERFACE include dir; kept
# out of the install(FILES) set below so package consumers never see the macro.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ident.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/platemaker/ident.hpp"
    @ONLY
)

# ---------------------------------------------------------------------------
# Credits — SPDX SBOM + third-party licence texts
#
# Emits credits/ (an SPDX 2.3 SBOM describing libplatemaker and its bundled deps, plus a copy of
# each dependency's licence text). The SBOM is the machine-readable inventory demanded by the EU
# Cyber Resilience Act and by commercial consumers; the licence texts satisfy the LGPL requirement
# to distribute a copy of the licence. The GUI merges this into a product-level SBOM (it locates the
# directory via platemaker_CREDITS_DIR, exported by the package config).
#
# Component versions are the build-time values (an SBOM records what was built against); the About
# dialog shows the honest *runtime* versions separately via Infrastructure::linkedComponents().
# ---------------------------------------------------------------------------
# Build-time dependency versions for the SBOM (libvips discovery differs by platform — see the root
# CMakeLists.txt vips section; on Windows both toolchains fetch the pinned web zip, Linux uses pkg-config).
if(WIN32)
    set(_pm_vips_version "${PLATEMAKER_VIPS_WEB_VERSION}")   # pinned web-zip tag (root CMakeLists.txt)
else()
    set(_pm_vips_version "${VIPS_VERSION}")                  # from pkg_check_modules (Linux)
endif()
if(DEFINED nlohmann_json_VERSION)
    set(_pm_json_version "${nlohmann_json_VERSION}")        # system/FetchContent-reported version
else()
    set(_pm_json_version "${PLATEMAKER_JSON_VERSION}")      # pinned tag (root CMakeLists.txt)
endif()

set(_pm_credits_dir "${CMAKE_CURRENT_BINARY_DIR}/credits")
file(MAKE_DIRECTORY "${_pm_credits_dir}/licenses")

# Licence texts are vendored in cmake/licenses/ (canonical SPDX / upstream texts) so the package is
# deterministic across toolchains: MSYS2 ships no libvips COPYING, the web zip ships only its own LGPL,
# and a system nlohmann install has no source tree to copy from. Copy the whole vendored set into
# credits/licenses/; THIRD-PARTY-NOTICES.txt and the SBOM reference these by file name. (libplatemaker's
# own LGPL-3.0 text is the same LGPL-3.0-or-later.txt.)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/licenses/"
     DESTINATION "${_pm_credits_dir}/licenses")

# Third-party NOTICES + the bundled-dependency SBOM fragments (@_pm_extra_packages@ /
# @_pm_extra_relationships@ below). Only meaningful on Windows, where we bundle the libvips DLL graph and
# the compiler runtime; on Linux libvips is a system package and nothing is bundled, so the SBOM stays
# the three core packages and no per-DLL notices are produced. The generator pulls authoritative versions
# from the web-zip's versions.json and licence/copyright from cmake/third_party.json.
set(_pm_extra_packages "")
set(_pm_extra_relationships "")
if(WIN32 AND DEFINED VIPS_ROOT_DIR)
    set(_pm_versions_file "${VIPS_ROOT_DIR}/versions.json")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_credits.cmake")
endif()

string(TIMESTAMP _pm_sbom_created "%Y-%m-%dT%H:%M:%SZ" UTC)
string(TIMESTAMP _pm_sbom_stamp   "%Y%m%d%H%M%S"       UTC)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sbom.spdx.json.in"
               "${_pm_credits_dir}/sbom.spdx.json" @ONLY)

# Windows DLL symbol export.
# MSVC: WINDOWS_EXPORT_ALL_SYMBOLS generates a .def file automatically.
# MinGW/GCC: the equivalent is the --export-all-symbols linker flag;
#   WINDOWS_EXPORT_ALL_SYMBOLS is silently ignored by CMake for GCC.
if(MSVC)
    set_target_properties(platemaker-lib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
elseif(WIN32)
    target_link_options(platemaker-lib PRIVATE "-Wl,--export-all-symbols")
endif()

# ---------------------------------------------------------------------------
# Install rules — produces a libplatemaker-dev package
#
# After building, run:
#   cmake --install build/<preset> [--config Release]
#
# This installs to the installDir set in CMakePresets.json:
#   install/<preset>/
#     bin/          platemaker.dll             (Windows runtime)
#     lib/          platemaker.lib             (Windows import lib / Linux .so)
#     lib/cmake/platemaker/
#                   platemaker-config.cmake
#                   platemaker-config-version.cmake
#                   platemaker-targets.cmake
#     include/platemaker/                      (all public headers)
#
# The Qt GUI project consumes the package with:
#   find_package(platemaker CONFIG REQUIRED)
#   target_link_libraries(my-target PRIVATE Platemaker::platemaker)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# CPack components (see root CMakeLists.txt for grouping → archives):
#   Development → headers, cmake config, import lib   ┐ grouped into the
#   Runtime     → shared lib (.dll/.so) + bundled DLLs ┘ platemaker-dev archive
#   cli         → platemaker-cli + a self-contained copy of the runtime
# The shared library lands in Runtime; the import lib (Windows ARCHIVE) in Development.
install(TARGETS platemaker-lib
    EXPORT platemaker-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime      # .dll on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime      # .so on Linux
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development  # .lib / .dll.a import lib
)

# Second install of the same target into the "cli" component so the standalone
# platemaker-cli archive is self-contained (no EXPORT here — Development owns it).
install(TARGETS platemaker-lib
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT cli
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/platemaker/platemaker_export.h"
    "${CMAKE_CURRENT_BINARY_DIR}/platemaker/version.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/platemaker
    COMPONENT Development
)

# credits/ (SBOM + licence texts) at the package root, in both distributables: the dev package
# (consumed by the GUI) and the standalone cli archive.
install(DIRECTORY "${_pm_credits_dir}/"
    DESTINATION credits
    COMPONENT Development
)
install(DIRECTORY "${_pm_credits_dir}/"
    DESTINATION credits
    COMPONENT cli
)

install(EXPORT platemaker-targets
    FILE     platemaker-targets.cmake
    NAMESPACE Platemaker::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
    COMPONENT Development
)

# Record the build-time toolchain so find_package() can reject an ABI-incompatible consumer (see the
# check in platemaker-config.cmake.in). The MSVC-ABI flag is the decisive axis on Windows: it is true
# for MSVC and clang-cl (MSVC ABI) and false for MinGW/GCC — the boundary that cannot be crossed.
set(PLATEMAKER_BUILT_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
if(MSVC)
    set(PLATEMAKER_BUILT_MSVC_ABI 1)
else()
    set(PLATEMAKER_BUILT_MSVC_ABI 0)
endif()

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/platemaker-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
)

# SameMinorVersion while the lib is pre-1.0: under semver a 0.MINOR bump is exactly where breaking
# changes live (0.1.1 → 0.2.0 already was one), so SameMajorVersion — which treats every 0.y as
# compatible — would let a consumer pinned to 0.3.0 silently accept an incompatible 0.4.0. This
# accepts only the same 0.MINOR.* and still rejects anything older than requested. Revisit at 1.0.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
    COMPONENT Development
)

# ---------------------------------------------------------------------------
# Windows: bundle runtime DLLs (libvips + transitive deps) into bin/.
#
# Both toolchains now source libvips from the same FetchContent web zip (VIPS_ROOT_DIR/bin).
# MSVC  — copy the whole (already minimal) web-zip bin/; the MSVC C++ runtime comes from the system.
# MinGW — the web zip supplies libvips + its deps (incl. its own LLVM libc++), but NOT the MinGW C++
#         runtime (libstdc++/libgcc/libwinpthread), which lives in the compiler's bin/. So the install
#         computes the transitive closure across BOTH directories and prunes to what is actually used.
#
# The POST_BUILD copy puts the DLLs next to the built library so the binary runs directly from the
# build tree (without cmake --install) and Windows loads the FetchContent vips, not any other on PATH.
# ---------------------------------------------------------------------------
# Helper: emit the MinGW DLL-closure install rules for a given CPack component.
# Called once per component (Runtime + cli) so both archives are self-contained.
# Reads _vips_bin_dir from the calling scope — a ';'-list of directories to search.
#
# DLL pruning: instead of copying whole bin/ directories, compute the actual transitive
# runtime-dependency closure of platemaker.dll at install time via file(GET_RUNTIME_DEPENDENCIES) —
# only libvips + its web-zip deps (GLib stack, codecs, libc++/libunwind) and the
# libstdc++/libgcc/libwinpthread MinGW runtime.
#
# NOTE: GET_RUNTIME_DEPENDENCIES walks the PE import table, so it will NOT catch
# codecs loaded dynamically as vips modules (lib/vips-modules-8.x). The formats
# used here (PNG/JPEG/WebP) are linked directly into libvips, so they are covered
# — verify with a real render after changing formats.
function(_pm_install_mingw_dll_closure _component)
    # First install(CODE) is configure-time expanded to hand three values to the
    # install script: the library path (genex), the vips bin/ search dir, and
    # CMAKE_INSTALL_BINDIR (not defined inside the install script). The second
    # uses a bracket-argument to keep ${...} unexpanded until install time.
    install(CODE "set(_pm_lib     \"$<TARGET_FILE:platemaker-lib>\")
set(_pm_search  \"${_vips_bin_dir}\")
set(_pm_bindir  \"${CMAKE_INSTALL_BINDIR}\")" COMPONENT ${_component})

    install(CODE [==[
        # Normalize backslash paths (e.g. C:\Windows\system32\kernel32.dll) to forward slashes before
        # matching, so the POST_EXCLUDE_REGEXES "[Ss]ystem32" reliably drops system DLLs — and, on
        # CMake 3.31+, so this call stops emitting a CMP0207 "policy not set" warning per system DLL
        # (dozens in CI). Guarded: older CMake that predates the policy just skips it.
        if(POLICY CMP0207)
            cmake_policy(SET CMP0207 NEW)
        endif()

        # _pm_search is a ';'-list of directories; it MUST be unquoted here so it expands to one
        # DIRECTORIES argument per directory. A quoted "${_pm_search}" is passed as a single "a;b"
        # string that GET_RUNTIME_DEPENDENCIES does NOT split, silently searching neither dir (that
        # loses the MinGW runtime from the compiler bin/). Unquoted list expansion splits on ';' only,
        # so directory paths containing spaces stay intact.
        file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES                   "${_pm_lib}"
            RESOLVED_DEPENDENCIES_VAR   _res
            UNRESOLVED_DEPENDENCIES_VAR _unres
            DIRECTORIES                 ${_pm_search}
            PRE_EXCLUDE_REGEXES  "^api-ms-" "^ext-ms-"
            POST_EXCLUDE_REGEXES "[Ss]ystem32"
        )
        foreach(_dep IN LISTS _res)
            file(INSTALL "${_dep}"
                 DESTINATION "${CMAKE_INSTALL_PREFIX}/${_pm_bindir}"
                 FOLLOW_SYMLINK_CHAIN)
        endforeach()
        if(_unres)
            message(FATAL_ERROR "Unresolved runtime deps: ${_unres}")
        endif()
    ]==] COMPONENT ${_component})
endfunction()

if(WIN32)
    if(MSVC AND DEFINED VIPS_ROOT_DIR)
        add_custom_command(TARGET platemaker-lib POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VIPS_ROOT_DIR}/bin"
                "$<TARGET_FILE_DIR:platemaker-lib>"
            COMMENT "Copying vips runtime DLLs to build output"
            VERBATIM
        )
        # MSVC keeps the full vips package; bundle it into both archives.
        foreach(_comp Runtime cli)
            install(
                DIRECTORY "${VIPS_ROOT_DIR}/bin/"
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT ${_comp}
                FILES_MATCHING PATTERN "*.dll"
            )
        endforeach()
    elseif(NOT MSVC AND DEFINED VIPS_ROOT_DIR)
        # MinGW: make the build tree runnable by copying the web-zip DLLs next to the binaries (the
        # MinGW C++ runtime is found via the developer's PATH); install then prunes to the real closure.
        add_custom_command(TARGET platemaker-lib POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VIPS_ROOT_DIR}/bin"
                "$<TARGET_FILE_DIR:platemaker-lib>"
            COMMENT "Copying libvips web-build DLLs to build output"
            VERBATIM
        )

        # Search both sources for the dependency closure: the web zip (libvips + its deps) and the
        # compiler's bin/ (libstdc++/libgcc/libwinpthread — not shipped in the web zip).
        get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(_vips_bin_dir "${VIPS_ROOT_DIR}/bin" "${_mingw_bin_dir}")

        _pm_install_mingw_dll_closure(Runtime)
        _pm_install_mingw_dll_closure(cli)
    endif()
endif()