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

#include <functional>
#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Core {

/// Progress report emitted after each slice is saved.
struct ProcessingProgress {
    int         sliceDone  = 0; //!< Number of slices saved so far (1-based count).
    int         sliceTotal = 0; //!< Expected total slice count for this run.
    std::string sliceName;      //!< Filename of the slice just saved, e.g. "output_003.png".
};

/// Severity of a pipeline log line.
enum class ProcessingLogLevel { Info, Warning, Error };

/// Result of a pipeline run.  The caller applies \c records to the project.
struct ProcessingOutcome {
    std::vector<Models::ProcessingSliceRecord> records;      //!< One per saved slice, in order.
    std::vector<std::string>                   skippedPages; //!< Inputs that were skipped (missing / unmatched / load error).
    bool        cancelled = false; //!< True if cancellation cut the run short.
    bool        failed    = false; //!< True if a fatal error aborted the run.
    std::string errorMessage;      //!< Human-readable message when \c failed is true.
};

/**
 * \class ProcessingPipeline
 * \brief Stateless runner for the scale → strip → slice → save pipeline.
 *
 * \c run() operates on a **copy** of the input file list (never the live
 * \c ProjectItem), so it is safe to invoke from a worker thread while the GUI
 * holds the workspace.  Callbacks fire on the calling thread.
 */
class PLATEMAKER_EXPORT ProcessingPipeline {
public:
    using ProgressFn   = std::function<void(const ProcessingProgress&)>;
    using LogFn        = std::function<void(ProcessingLogLevel, const std::string&)>;
    using SliceSavedFn = std::function<void(const std::string& name,
                                            const std::string& fullPath)>;

    ProcessingPipeline() = default;

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
     * \param onProgress       Invoked after each slice is saved (may be empty).
     * \param onLog            Invoked for informational / warning / error lines (may be empty).
     * \param onSliceSaved     Invoked with (filename, full path) after each save (may be empty).
     * \return A \c ProcessingOutcome with per-slice records, skipped pages and flags.
     */
    [[nodiscard]] ProcessingOutcome run(
        const std::vector<Models::InputFile>&      inputs,
        const Models::OutputProfile&               outProfile,
        const std::vector<Models::CanvasProfile>&  canvasProfiles,
        const std::vector<std::string>&            canvasProfileIds,
        const std::string&                         outputDir,
        const Infrastructure::CancellationToken&   cancel,
        const ProgressFn&                          onProgress   = {},
        const LogFn&                               onLog        = {},
        const SliceSavedFn&                        onSliceSaved = {}) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP
