cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(PLATEMAKER_SOURCES
    # Models (non-trivial computed properties)
    src/models/canvas_profile.cpp

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
# ---------------------------------------------------------------------------
target_link_libraries(platemaker-lib
    PUBLIC
        PkgConfig::VIPS
        nlohmann_json::nlohmann_json
)

# ThumbnailCache uses QtConcurrent in GUI builds; synchronous fallback in CLI-only builds.
# NOTE: When PLATEMAKER_BUILD_GUI=ON the CLI binary will transitively link Qt6::Concurrent
#       as well. Use the linux-cli-only preset for a truly Qt-free CLI binary.
if(PLATEMAKER_BUILD_GUI)
    target_link_libraries(platemaker-lib PUBLIC Qt6::Concurrent)
endif()

# ---------------------------------------------------------------------------
# Compiler features
# ---------------------------------------------------------------------------
target_compile_features(platemaker-lib PUBLIC cxx_std_20)

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
