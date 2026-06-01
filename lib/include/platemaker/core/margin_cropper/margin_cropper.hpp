/**
 * \file
 * \brief MarginCropper — crops margin zones from a source image, producing the safe area.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_MARGIN_CROPPER_HPP
#define PLATEMAKER_CORE_MARGIN_CROPPER_HPP

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Core {

/**
 * \class MarginCropper
 * \brief Removes margin zones from a pixel buffer, returning only the safe area.
 *
 * MarginCropper performs a pure crop — no resampling, no colour transformation.
 * It runs as the first step of the margin-aware import pipeline, before Scaler,
 * so that the subsequent scaling operation sees only the safe-area pixels.
 *
 * MarginCropper is stateless and thread-safe.
 *
 * \note The margins passed to \c crop() must be in the coordinate space of the
 *       source image (i.e. the canvas pixel dimensions, not the scaled dimensions).
 *       Always crop before scaling, not after.
 */
class MarginCropper {
public:
    MarginCropper() = default;

    /**
     * \brief Crops all four margin zones from \p source and returns the safe area.
     *
     * The output dimensions are:
     * - width  = source.width()  - margins.left - margins.right
     * - height = source.height() - margins.top  - margins.bottom
     *
     * \param source  The original (un-cropped) image buffer.  Must be valid.
     * \param margins Margin sizes in pixels for each side of the canvas.
     *                All margin values must be non-negative.  The sum of
     *                left + right must be less than source.width(), and
     *                top + bottom must be less than source.height().
     * \return A new PixelBuffer containing only the safe-area region.
     *
     * \throws std::invalid_argument if the margins would produce a zero or negative safe area.
     * \throws std::runtime_error    if the libvips crop operation fails.
     */
    [[nodiscard]] PixelBuffer crop(
        const PixelBuffer&       source,
        const Models::Margins&   margins) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_MARGIN_CROPPER_HPP
