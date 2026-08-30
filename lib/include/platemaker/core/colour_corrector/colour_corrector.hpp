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
 * (\c ImageIO::load(path, convertToSRGB)), gated by \c ColourCorrection::iccToSRGB.  ColourCorrector
 * owns only the creative scalars, so a page loaded straight (no ICC) and one converted to sRGB both
 * receive the identical grade.
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
     * \param cc     The grade to apply.  \c cc.iccToSRGB and \c cc.excludedInputUids are the caller's
     *               concern (ICC at load; exclusion decided before calling) — ignored here.
     * \return The graded buffer (or \p buffer unchanged for a neutral grade).
     * \throws std::runtime_error if \p buffer is invalid or a libvips operation fails.
     */
    [[nodiscard]] PixelBuffer apply(PixelBuffer buffer, const Models::ColourCorrection& cc) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_COLOUR_CORRECTOR_HPP
