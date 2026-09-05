/**
 * \file lib/include/platemaker/core/colour_corrector/colour_corrector.hpp
 * \brief ColourCorrector — applies a project's point-grade (brightness / contrast / saturation) to one
 *        source page during render.
 *
 * This is the page-domain half of the optional colour-correction step (see \c Models::ColourCorrection).
 * It is a *point operation*: every output pixel depends only on the matching input pixel, so applying
 * the same grade per page is pixel-identical to grading the whole strip — which is exactly why the grade
 * can be project-wide yet still allow per-page exclusions.
 *
 * ICC → sRGB is deliberately **not** done here: it is a colour-normalisation concern handled at load
 * (\c ImageIO::decode(path, convertToSRGB)).  ColourCorrector owns only the creative scalars, so a page
 * loaded straight (no ICC) and one converted to sRGB both receive the identical grade.
 *
 * Stateless and thread-safe, like \c Scaler / \c MarginCropper — construct once, call \c apply() freely.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_COLOUR_CORRECTOR_HPP
#define PLATEMAKER_CORE_COLOUR_CORRECTOR_HPP

#include "platemaker/platemaker_export.h"
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/processing_steps.hpp>

namespace Platemaker::Core {

/**
 * \class ColourCorrector
 * \brief Applies the brightness / contrast / saturation point grade of a \c ColourCorrection to a page.
 */
class PLATEMAKER_EXPORT ColourCorrector {
public:
    ColourCorrector() = default;

    /**
     * \brief Returns \p buffer graded per \p cc (brightness → contrast → saturation).
     *
     * A neutral grade (brightness 0, contrast 1, saturation 1) returns \p buffer unchanged — no pixels
     * are touched, so enabling the step with default scalars is a true no-op.  Saturation is
     * luminance-preserving (Rec.709 weights) and only applied to 3-band colour; an alpha channel, if
     * present, is split off before grading and re-attached untouched.  The result is cast back to the
     * source band format (clipping to its range), so the downstream pipeline sees the same format it
     * would for an ungraded page.
     *
     * \param buffer Source page pixels (ownership transferred in).  Must be valid.
     * \param cc     The grade to apply.  \c cc.excludedInputUids is the caller's concern (the exclusion is
     *               decided before calling) — ignored here.
     * \return The graded buffer (or \p buffer unchanged for a neutral grade).
     * \throws std::runtime_error if \p buffer is invalid or a libvips operation fails.
     */
    [[nodiscard]] PixelBuffer applyToBuffer(PixelBuffer buffer, const Models::ColourCorrection& cc) const;

    /**
     * \brief Grades an interleaved 8-bit RGBA buffer in place, reusing \c apply().
     *
     * \p rgba is \p width × \p height × 4 bytes (R,G,B,A per pixel, row-major). It is wrapped in a libvips
     * image tagged sRGB (so the 4th band is treated as alpha), graded by \c apply(), and the result copied
     * back over \p rgba. A neutral grade leaves the bytes untouched.
     *
     * This is the entry point a GUI uses for a live grade *preview* of an already-decoded output slice —
     * libvips stays inside the lib, so the consumer needs no vips dependency. Because it is a point grade,
     * grading a decoded output slice matches grading the source page then re-slicing (see the class doc),
     * so the preview equals the committed render for the brightness / contrast / saturation / curve
         *
     * \param rgba   Interleaved RGBA8888 pixels, graded in place. Must be non-null and hold width*height*4 bytes.
     * \param width  Image width in pixels (> 0).
     * \param height Image height in pixels (> 0).
     * \param cc     The grade to apply (same semantics as \c apply()).
     * \throws std::runtime_error on invalid input or a libvips failure.
     */
    void applyToRgba(unsigned char* rgba, int width, int height, const Models::ColourCorrection& cc) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_COLOUR_CORRECTOR_HPP
