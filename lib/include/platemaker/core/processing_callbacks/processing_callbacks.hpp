/**
 * \file lib/include/platemaker/core/processing_callbacks/processing_callbacks.hpp
 * \brief ProcessingCallbacks — the set of progress/event callbacks a caller may pass to
 *        ProcessingPipeline::run(), plus the small payload types each one reports.
 *
 * Split out of processing_pipeline.hpp so the pipeline header stays focused on the pipeline
 * itself. Every callback is an ordinary std::function; a null field means "don't report this".
 * The pipeline calls them **synchronously on its calling thread** and knows nothing about
 * threads or the GUI — a consumer that runs the pipeline on a worker thread is responsible for
 * marshalling the data to wherever it reacts (e.g. the GUI thread). Keep the reactions short so
 * the render proceeds at its own pace.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-26
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_PROCESSING_CALLBACKS_HPP
#define PLATEMAKER_CORE_PROCESSING_CALLBACKS_HPP

#include <functional>
#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

namespace Platemaker::Core {

/// Progress report emitted after each slice is saved.
struct ProcessingProgress {
    int         sliceDone  = 0; //!< Number of slices saved so far (1-based count).
    int         sliceTotal = 0; //!< Expected total slice count for this run.
    std::string sliceName;      //!< Filename of the slice just saved, e.g. "output_003.png".
};

/// Severity of a pipeline log line.
enum class ProcessingLogLevel { Info, Warning, Error };

/// Outcome of processing one input file during phase 1 (strip building).
enum class InputStatus {
    Appended,                 //!< Loaded, scaled and appended to the strip using a matched canvas profile,
                              //!< or with no profile at all when the project uses no canvas profiles.
    AppendedWithoutProfile,   //!< Canvas profiles are in use but none — anywhere — matches this size, so the
                              //!< page was rendered implicitly (scaled, no margins). Not an error.
    AppendedProfileNotLinked, //!< Rendered implicitly (scaled, no margins), but a profile of this size exists
                              //!< in the workspace unlinked; see \c InputResult::unlinkedCandidateProfileIds.
                              //!< Link it to apply its margins.
    SkippedMissing,          //!< File was FileStatus::Missing.
    SkippedNoProfile,        //!< Retained but not currently emitted: the pipeline renders unmatched pages
                             //!< implicitly (see \c AppendedWithoutProfile). Kept for a future opt-in skip mode.
    SkippedProfileNotLinked, //!< Retained but not currently emitted: a same-size profile exists unlinked, yet
                             //!< the pipeline now renders such pages implicitly (see \c AppendedProfileNotLinked).
                             //!< Kept for a future opt-in skip mode; would carry \c unlinkedCandidateProfileIds.
    SkippedError,            //!< Load / margin-crop / scale failed; see \c InputResult::detail.
};

/// Reported once per input via ProcessingCallbacks::onInput.
struct InputResult {
    std::string inputPath;                            //!< Absolute path of the input file.
    InputStatus status = InputStatus::Appended;       //!< What happened to it.
    std::vector<std::string> unlinkedCandidateProfileIds; //!< Only for \c SkippedProfileNotLinked: ids of
                                                          //!< workspace profiles matching this image's size.
    std::string detail;                               //!< Human note for \c SkippedError (the error text); else empty.
};

/// Reported once, after the strip is assembled, before the first slice — via onSlicingStarted.
struct SlicingStarted {
    int expectedSliceCount = 0; //!< Total slices the assembled strip will produce (all of them, before any partial filter).
};

/// Reported after each slice file is written — via onSliceSaved.
struct SliceSaved {
    int         sliceIndex = 0; //!< 0-based output index (== the output tile's row). Output number is sliceIndex + startIndex.
    std::string name;           //!< Slice filename, e.g. "output_003.png".
    std::string fullPath;       //!< Absolute path the slice was written to.
};

/// Reported for a slice a partial re-render leaves untouched — via onSliceSkipped.
struct SliceSkipped {
    int         sliceIndex = 0; //!< 0-based output index of the skipped (clean) slice.
    std::string name;           //!< Slice filename that was not re-rendered.
};

/**
 * \brief The callbacks a caller may pass to ProcessingPipeline::run().
 *
 * Each field is optional — a null std::function is simply not called. The pipeline invokes them
 * synchronously on its own thread; consumers own any threading/marshalling.
 */
struct PLATEMAKER_EXPORT ProcessingCallbacks {
    std::function<void(const ProcessingProgress&)>                 onProgress;       //!< After each slice is saved (progress tick).
    std::function<void(ProcessingLogLevel, const std::string&)>    onLog;            //!< Human-readable info/warning/error lines.
    std::function<void(const InputResult&)>                        onInput;          //!< Once per input in phase 1 (appended or skipped-with-reason).
    std::function<void(const SlicingStarted&)>                     onSlicingStarted; //!< Once, at the phase-1 → phase-2 transition.
    std::function<void(const SliceSaved&)>                         onSliceSaved;     //!< After each slice file is written.
    std::function<void(const SliceSkipped&)>                       onSliceSkipped;   //!< For each clean slice a partial re-render skips.
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_CALLBACKS_HPP
