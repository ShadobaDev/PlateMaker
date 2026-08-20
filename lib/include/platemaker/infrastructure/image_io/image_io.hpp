/**
 * \file lib/include/platemaker/infrastructure/image_io/image_io.hpp
 * \brief ImageIO — libvips-backed image loading and saving with per-file error resilience.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_IMAGE_IO_HPP
#define PLATEMAKER_INFRASTRUCTURE_IMAGE_IO_HPP

#include <stdexcept>
#include <string>

#include "platemaker/platemaker_export.h"

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Infrastructure {

// Cross-module visibility for a thrown exception type. The two toolchains match exceptions across a
// shared-library boundary differently, so the correct annotation differs:
//   * MSVC matches by the type's decorated *name*, which every module that includes this header already
//     shares — so no __declspec(dllexport) is needed. Exporting one would only trigger C4275 (a
//     non-dll-interface std base) and LNK4197 (the vtable exported from every TU), for no benefit.
//   * GCC/Clang build the lib with hidden visibility, where the type_info must have *default* visibility
//     for a consumer in another shared object to catch it by type — so it keeps PLATEMAKER_EXPORT.
#if defined(_MSC_VER)
#  define PLATEMAKER_EXCEPTION_EXPORT
#else
#  define PLATEMAKER_EXCEPTION_EXPORT PLATEMAKER_EXPORT
#endif

/**
 * \brief Thrown by \c ImageIO::save() when the destination cannot be published because another process
 *        holds it open (Explorer's preview, antivirus, an image viewer).
 *
 * Distinct from a generic write failure so a caller (the pipeline) can report it as the typed
 * \c Models::ProcessingErrorCode::OutputLocked and leave any retry policy to the consumer — the lib
 * itself never polls. The processing pipeline catches it internally, so higher-level consumers see the
 * ProcessingErrorCode rather than this type; a direct \c ImageIO::save() caller can still catch it by
 * type across the library boundary (the unit tests do) — see \c PLATEMAKER_EXCEPTION_EXPORT above.
 */
class PLATEMAKER_EXCEPTION_EXPORT OutputLockedError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * \class ImageIO
 * \brief Reads and writes image files via libvips.
 *
 * ImageIO abstracts all libvips file IO and provides a unified interface for the
 * rest of libplatemaker.  It handles:
 * - Colour profile normalisation on load (sRGB conversion).
 * - Format dispatch on save (PNG, JPEG, WebP).
 * - Per-file error isolation: a failed load throws an exception that the caller
 *   can catch and log, without aborting the entire batch.
 *
 * ImageIO is stateless and thread-safe.
 *
 * \note Supported input formats: PNG, JPEG, TIFF (anything libvips can open).
 *       Supported output formats: PNG, JPEG, WebP — as selected by OutputFormat.
 */
class PLATEMAKER_EXPORT ImageIO {
public:
    ImageIO() = default;

    /**
     * \brief Loads an image from disk and returns it as a PixelBuffer.
     *
     * Uses libvips sequential (streaming) access — the full image is not loaded
     * into RAM upfront.  The colour profile embedded in the file is normalised to
     * sRGB on load if present.
     *
     * \param filePath Absolute path to the image file to load.
     * \return A valid PixelBuffer containing the loaded image.
     *
     * \throws std::runtime_error if the file does not exist, cannot be read, or is
     *                            not a supported format.
     */
    [[nodiscard]] Core::PixelBuffer load(const std::string& filePath) const;

    /**
     * \brief Saves a PixelBuffer to disk using the profile's format and options.
     *
     * The container format is taken from \c profile.outputFormat and the matching
     * per-format option struct (\c jpegOptions / \c pngOptions / \c webpOptions) is
     * applied; the others are ignored.
     *
     * The write is **atomic**: the bytes are encoded to a temporary sibling and then renamed over
     * \p outputPath, so a reader never sees a half-written file and \p outputPath is replaced whole.
     *
     * \param buffer     The pixel data to save.  Must be valid.
     * \param outputPath Absolute path of the output file.  The file is created or
     *                   overwritten.  Parent directories must exist.
     * \param profile    Output profile supplying the format and encoding options.
     *
     * \throws OutputLockedError if the destination cannot be replaced because another process holds it
     *                           open (the lib does not retry — see \c ProcessingErrorCode::OutputLocked).
     * \throws std::runtime_error if encoding otherwise fails.
     */
    void save(
        const Core::PixelBuffer&     buffer,
        const std::string&           outputPath,
        const Models::OutputProfile& profile) const;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_IMAGE_IO_HPP
