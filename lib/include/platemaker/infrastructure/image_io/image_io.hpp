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

#include <string>

#include "platemaker/platemaker_export.h"

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Infrastructure {

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
     * \param buffer     The pixel data to save.  Must be valid.
     * \param outputPath Absolute path of the output file.  The file is created or
     *                   overwritten.  Parent directories must exist.
     * \param profile    Output profile supplying the format and encoding options.
     *
     * \throws std::runtime_error if the write operation fails.
     */
    void save(
        const Core::PixelBuffer&     buffer,
        const std::string&           outputPath,
        const Models::OutputProfile& profile) const;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_IMAGE_IO_HPP
