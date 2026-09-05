/**
 * \file lib/include/platemaker/models/colour_correction.hpp
 * \brief ColourCorrection - the page-domain grade, and the tone curves it carries.
 *
 * One of the two optional processing steps.  It lives in its own header rather than beside the
 * overlay types because the two features share nothing but the word 'step': this one is numbers
 * applied to a page before it is scaled, the other is bitmaps composited onto an assembled
 * strip.  The framework that enumerates both is \c processing_steps.hpp.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_COLOUR_CORRECTION_HPP
#define PLATEMAKER_MODELS_COLOUR_CORRECTION_HPP

#include <algorithm>
#include <string>
#include <vector>

namespace Platemaker::Models {

/**
 * \brief One control point of a tone curve, in normalised [0,1] input/output.
 */
struct CurvePoint {
    double x = 0.0; //!< Input value, 0..1.
    double y = 0.0; //!< Output value, 0..1.
};

/**
 * \brief Tone curves: a master (all-channels) curve plus optional per-channel curves.
 *
 * Each curve is a list of control points the renderer interpolates (linearly, MVP) into a lookup
 * table.  An empty curve is the identity.  The master curve applies first; a per-channel curve then
 * maps the master-adjusted value for its channel — so the effective map is
 * \c channelCurve(masterCurve(v)).  Points may be given in any order (the renderer sorts by x).
 */
struct ColourCurves {
    std::vector<CurvePoint> master; //!< Applied to all channels first (empty = identity).
    std::vector<CurvePoint> red;    //!< Red channel, applied after master (empty = identity).
    std::vector<CurvePoint> green;  //!< Green channel.
    std::vector<CurvePoint> blue;   //!< Blue channel.
};

//! True when any of the four curves carries control points (i.e. is not the identity).
[[nodiscard]] inline bool hasAnyCurve(const ColourCurves& c)
{
    return !c.master.empty() || !c.red.empty() || !c.green.empty() || !c.blue.empty();
}

/**
 * \brief Project-wide colour correction applied per input page at render time (page domain).
 *
 * Non-destructive: the source files are never modified — the grade is applied to a copy in the
 * pipeline.  \c enabled gates the whole step; with it \c false the pipeline does no colour work and
 * the output is byte-identical to a build without this feature.
 *
 * Apply order within the grade: tone curves → brightness/contrast → saturation.
 */
struct ColourCorrection {
    bool enabled = false; //!< Master toggle. When false the step is skipped entirely.

    ColourCurves curves;     //!< Per-channel tone curves (empty = identity). Applied first, 8-bit only (MVP).
    double brightness = 0.0; //!< Additive lift, roughly [-1, 1]; 0 = no change.
    double contrast   = 1.0; //!< Multiplicative contrast around mid-grey; 1 = no change.
    double saturation = 1.0; //!< Chroma scale; 1 = no change, 0 = greyscale.

    /**
     * \brief Input \c uid values this grade skips (e.g. a title or end page).
     *
     * Keyed by \c InputFile::uid (not path) so a rename does not silently un-exclude a page.  An
     * excluded page is rendered exactly as it would be with the whole step disabled.
     */
    std::vector<std::string> excludedInputUids;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_COLOUR_CORRECTION_HPP
