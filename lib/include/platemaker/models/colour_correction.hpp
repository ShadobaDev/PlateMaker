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
 * pipeline.
 *
 * **There is no master toggle: a neutral grade is no grade.**  \c isNeutral() is what the pipeline
 * gates on, so a project nobody has graded does no colour work and renders byte-identically to a build
 * without this feature.  A stored flag beside the values could disagree with them — and a consumer
 * then has two answers to "is this project graded" and no rule for which one wins.  Switching a grade
 * off is resetting it, which is undoable like any other edit.
 *
 * Apply order within the grade: tone curves → brightness/contrast → saturation.
 */
struct ColourCorrection {
    ColourCurves curves;     //!< Per-channel tone curves (empty = identity). Applied first, 8-bit only (MVP).
    double brightness = 0.0; //!< Additive lift, roughly [-1, 1]; 0 = no change.
    double contrast   = 1.0; //!< Multiplicative contrast around mid-grey; 1 = no change.
    double saturation = 1.0; //!< Chroma scale; 1 = no change, 0 = greyscale.

    /**
     * \brief Input \c uid values this grade skips (e.g. a title or end page).
     *
     * Keyed by \c InputFile::uid (not path) so a rename does not silently un-exclude a page.  An
     * excluded page is rendered exactly as it would be if the grade were neutral.
     */
    std::vector<std::string> excludedInputUids;
};

/**
 * \brief True when this grade would leave every pixel exactly as it found it.
 *
 * The one test for "is this project graded", asked by the pipeline, by the staleness signature and by
 * any consumer previewing the grade — so that none of them can answer it differently.
 *
 * Compared exactly against the neutral values rather than within a tolerance: these are stored numbers
 * a user set, not computed ones, and a slider returned to its default has to read as untouched.
 *
 * \c excludedInputUids is deliberately not consulted.  Excluding pages from a grade that changes
 * nothing excludes them from nothing, and counting it would make a project "graded" with no way for
 * the user to see why.
 */
[[nodiscard]] inline bool isNeutral(const ColourCorrection& cc) noexcept
{
    return cc.brightness == 0.0 && cc.contrast == 1.0 && cc.saturation == 1.0
        && !hasAnyCurve(cc.curves);
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_COLOUR_CORRECTION_HPP
