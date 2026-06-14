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

    # Core pipeline
    src/core/pixel_buffer/pixel_buffer.cpp
    src/core/scaler/scaler.cpp
    src/core/scaled_strip/scaled_strip.cpp
    src/core/slicer/slicer.cpp
    src/core/margin_cropper/margin_cropper.cpp
    src/core/template_generator/template_generator.cpp

    # Infrastructure / IO
    src/infrastructure/file/file_meta_data.cpp
    src/infrastructure/image_io/image_io.cpp
    src/infrastructure/workspace_serializer/workspace_serializer.cpp
    src/infrastructure/thumbnail_cache/thumbnail_cache.cpp
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

# Namespaced alias — consumers use Platemaker::platemaker in target_link_libraries
add_library(Platemaker::platemaker ALIAS platemaker-lib)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(platemaker-lib
    PUBLIC
        # Consumers include as: #include <platemaker/...>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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
        "${CMAKE_CURRENT_SOURCE_DIR}/include/platemaker/platemaker_export.h"
)

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

install(TARGETS platemaker-lib
    EXPORT platemaker-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}          # .dll on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}           # .so on Linux
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}           # .lib on Windows
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT platemaker-targets
    FILE     platemaker-targets.cmake
    NAMESPACE Platemaker::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/platemaker-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/platemaker-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/platemaker
)

# ---------------------------------------------------------------------------
# Windows: bundle runtime DLLs (libvips + transitive deps) into bin/.
#
# MSVC  — VIPS_ROOT_DIR is set by FetchContent; copy the whole vips bin/.
# MinGW — vips DLLs live in MSYS2 bin/ alongside all other runtime DLLs;
#         derive the directory from the pkg-config library dir and copy everything.
#
# The POST_BUILD copy puts the DLLs next to the built library so that the
# binary can be run directly from the build tree (without cmake --install)
# and Windows loads the FetchContent vips instead of any system/MinGW vips
# that might be on PATH.
# ---------------------------------------------------------------------------
if(WIN32)
    if(MSVC AND DEFINED VIPS_ROOT_DIR)
        add_custom_command(TARGET platemaker-lib POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VIPS_ROOT_DIR}/bin"
                "$<TARGET_FILE_DIR:platemaker-lib>"
            COMMENT "Copying vips runtime DLLs to build output"
            VERBATIM
        )
        install(
            DIRECTORY "${VIPS_ROOT_DIR}/bin/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.dll"
        )
    elseif(NOT MSVC AND DEFINED VIPS_LIBRARY_DIRS)
        # MinGW/MSYS2: vips DLLs live alongside all other MSYS2 DLLs in the same bin/.
        # Derive the directory from the lib path reported by pkg-config and copy
        # everything — same simple approach as MSVC above.
        list(GET VIPS_LIBRARY_DIRS 0 _vips_lib_dir)
        get_filename_component(_vips_bin_dir "${_vips_lib_dir}/../bin" ABSOLUTE)
        install(
            DIRECTORY "${_vips_bin_dir}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.dll"
        )
    endif()
endif()