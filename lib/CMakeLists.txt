cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# Source files — implementation stubs (populated during Stage 1 development)
# ---------------------------------------------------------------------------
set(LIBPLATEMAKER_SOURCES
    # Models (non-trivial computed properties)
    src/models/canvas_profile.cpp

    # Core pipeline
    src/cancellation_token.cpp
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

add_library(libplatemaker STATIC ${LIBPLATEMAKER_SOURCES})

# Expose as "platemaker" to consumers (avoids the lib/lib double prefix)
add_library(Platemaker::platemaker ALIAS libplatemaker)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(libplatemaker
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
target_link_libraries(libplatemaker
    PUBLIC
        PkgConfig::VIPS
        nlohmann_json::nlohmann_json
)

# ThumbnailCache uses QtConcurrent in GUI builds; synchronous fallback in CLI-only builds.
# NOTE: When PLATEMAKER_BUILD_GUI=ON the CLI binary will transitively link Qt6::Concurrent
#       as well. Use the linux-cli-only preset for a truly Qt-free CLI binary.
if(PLATEMAKER_BUILD_GUI)
    target_link_libraries(libplatemaker PUBLIC Qt6::Concurrent)
endif()

# ---------------------------------------------------------------------------
# Compiler features
# ---------------------------------------------------------------------------
target_compile_features(libplatemaker PUBLIC cxx_std_20)
