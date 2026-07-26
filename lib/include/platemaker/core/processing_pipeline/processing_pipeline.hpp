/**
 * \file lib/include/platemaker/core/processing_pipeline/processing_pipeline.hpp
 * \brief ProcessingPipeline — runs the full scale → strip → slice → save pipeline
 *        for one project, with progress, logging and cooperative cancellation.
 *
 * This is the single source of truth for the processing pipeline shared by the CLI
 * and the GUI.  It owns only the compute + disk-write phase; the caller is
 * responsible for resolving the project/profile/output directory, calling
 * \c ProjectItem::sanitize() / \c isUpToDate(), applying the returned records via
 * \c ProjectItem::applyProcessingResults(), and persisting the workspace.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-20
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP

#include <string>
#include <unordered_set>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Core {

// ProcessingProgress, ProcessingLogLevel and the ProcessingCallbacks set live in
// processing_callbacks.hpp (included above), keeping this header focused on the pipeline.

/// Result of a pipeline run.  The caller applies \c records to the project.
struct ProcessingOutcome {
    std::vector<Models::ProcessingSliceRecord> records;      //!< One per saved slice, in order.
    std::vector<std::string>                   skippedPages; //!< Inputs that were skipped (missing / unmatched / load error).
    std::vector<Models::AppliedCanvasProfile>  appliedProfiles; //!< Canvas profile applied per input (see the Models type).
    bool        cancelled = false; //!< True if cancellation cut the run short.
    bool        failed    = false; //!< True if a fatal error aborted the run.
    std::string errorMessage;      //!< Human-readable message when \c failed is true.
};

/**
 * \class ProcessingPipeline
 * \brief Stateless runner for the scale → strip → slice → save pipeline.
 *
 * Holds no state — \c run() is \c static; call it as \c ProcessingPipeline::run(...) without
 * constructing an instance. It operates on a **copy** of the input file list (never the live
 * \c ProjectItem), so it is safe to invoke from a worker thread while the GUI holds the workspace.
 * Callbacks fire on the calling thread.
 */
class PLATEMAKER_EXPORT ProcessingPipeline {
public:
    /**
     * \brief Builds the virtual strip from \p inputs, slices it, and saves every slice.
     *
     * \param inputs           Copy of the project's input files (paths + statuses).
     *                         Files with \c FileStatus::Missing are skipped.
     * \param outProfile       Output profile (target width, slice height, format, …).
     * \param canvasProfiles   Full workspace canvas-profile palette (may be empty →
     *                         no margin matching, plain scale).
     * \param canvasProfileIds Project-linked profile ids (empty → accept all).
     * \param outputDir        Existing directory where slice files are written.
     * \param cancel           Polled between slices; a partial result is returned on cancel.
     * \param callbacks        Optional progress/event callbacks (see \c ProcessingCallbacks); any
     *                         field may be null. Invoked synchronously on the calling thread.
     * \param onlySlices       Optional partial-render filter.  When non-null, only slices whose
     *                         output file name is in the set are encoded, hashed, saved and
     *                         recorded; all others are skipped (the strip is still assembled and
     *                         sliced once).  Null → render every slice (full render).
     * \return A \c ProcessingOutcome with per-slice records, skipped pages and flags.
     */
    [[nodiscard]] static ProcessingOutcome run(
        const std::vector<Models::InputFile>&      inputs,
        const Models::OutputProfile&               outProfile,
        const std::vector<Models::CanvasProfile>&  canvasProfiles,
        const std::vector<std::string>&            canvasProfileIds,
        const std::string&                         outputDir,
        const Infrastructure::CancellationToken&   cancel,
        const ProcessingCallbacks&                 callbacks    = {},
        const std::unordered_set<std::string>*     onlySlices   = nullptr);
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP
