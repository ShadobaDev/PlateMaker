/**
 * \file lib/include/platemaker/models/processing_results.hpp
 * \brief What a render reports back about itself, in Models terms.
 *
 * The two records a completed render hands to \c ProjectItem: one per saved slice, and one per
 * input saying which canvas profile was actually applied.  They live in Models rather than Core
 * precisely so \c ProjectItem can consume a render's results without depending on Core or on
 * anything image-processing.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROCESSING_RESULTS_HPP
#define PLATEMAKER_MODELS_PROCESSING_RESULTS_HPP

#include <string>
#include <vector>

#include "platemaker/models/project_files.hpp"

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// ProcessingSliceRecord
// ---------------------------------------------------------------------------

/**
 * \brief A processed slice descriptor passed to \c ProjectItem::applyProcessingResults().
 *
 * This is the Models-layer representation of one output slice — it carries
 * the same provenance data as \c Core::SliceResult but contains no pixel
 * buffer, keeping \c ProjectItem free from any Core or image-processing
 * dependency.
 *
 * The calling layer (CLI / GUI) is responsible for constructing these records
 * from \c Core::SliceResult objects after saving the pixel data to disk.
 */
struct ProcessingSliceRecord {
    std::string                fileName;     //!< Output filename, e.g. "output_001.png".
    std::string                outputSha256; //!< SHA-256 of the saved output file (may be empty).
    std::vector<SourceSegment> sourceMap;    //!< Provenance: one entry per contributing source file.
};

// ---------------------------------------------------------------------------
// AppliedCanvasProfile
// ---------------------------------------------------------------------------

/**
 * \brief Which canvas profile a processing run actually applied to one input.
 *
 * Editing a profile leaves both the input file and the output file byte-identical, so
 * no hash can notice that a page went stale.  This is the trace that makes it
 * detectable: the pipeline reports what it applied, \c applyProcessingResults() stores
 * it on the \c InputFile, and \c detectCanvasConfigChange() compares it next time.
 *
 * Lives in Models — like \c ProcessingSliceRecord — so \c ProjectItem can consume it
 * without depending on Core.
 */
struct AppliedCanvasProfile {
    std::string sourceFilePath; //!< Input this refers to.
    std::string profileId;      //!< Profile that matched ("" = none matched / project has no profiles).
    std::string fingerprint;    //!< canvasRenderFingerprint() of it ("" when profileId is empty).
    int         width  = 0;     //!< Display width the run resolved against (0 = not recorded).
    int         height = 0;     //!< Display height the run resolved against (0 = not recorded).
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROCESSING_RESULTS_HPP
