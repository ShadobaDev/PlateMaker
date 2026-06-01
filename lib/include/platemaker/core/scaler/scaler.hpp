/**
 * \file
 * \brief Scaler — scales a single source image to a target width using Lanczos3.
 *
 * Also defines ScaledImage, the value type produced by Scaler and consumed by ScaledStrip.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_SCALER_HPP
#define PLATEMAKER_CORE_SCALER_HPP

#include <string>

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>

namespace Platemaker::Core {

/**
 * \brief The output of a single Scaler::scale() call.
 *
 * Bundles the scaled pixel buffer with the original file path so that the rest
 * of the pipeline can track provenance (which input file contributed which pixels
 * to each output slice).
 *
 * ScaledImage is move-only because PixelBuffer is move-only.
 */
struct ScaledImage {
    PixelBuffer buffer;         //!< Scaled pixel data at the target width.
    std::string sourceFilePath; //!< Absolute path of the input file this image was scaled from.
};

/**
 * \class Scaler
 * \brief Scales a source image file to a target width preserving the aspect ratio.
 *
 * Scaler is stateless and thread-safe — a single instance may be called concurrently
 * from multiple threads.  It uses \c vips_thumbnail() internally, which employs
 * Lanczos3 resampling and operates in sequential (streaming) access mode so that
 * the entire source image is never loaded into RAM at once.
 *
 * \note Only width is specified; height is computed automatically to preserve the
 *       original aspect ratio.  This matches the Webtoon publish requirement where
 *       the horizontal dimension is constrained to 800 px.
 */
class Scaler {
public:
    Scaler() = default;

    /**
     * \brief Scales the image at \p filePath to \p targetWidth pixels wide.
     *
     * Reads the source file via libvips sequential access, scales using Lanczos3
     * anti-aliased downsampling, and returns the result in a ScaledImage.
     *
     * \param filePath    Absolute path to the source image file (PNG, JPEG, or TIFF).
     * \param targetWidth Target output width in pixels.  Must be greater than zero.
     * \return ScaledImage containing the scaled pixel buffer and the source file path.
     *
     * \throws std::runtime_error if the file cannot be loaded or the scaling operation fails.
     */
    [[nodiscard]] ScaledImage scale(const std::string& filePath, int targetWidth) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_SCALER_HPP
