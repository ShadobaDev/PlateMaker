/**
 * \file lib/include/platemaker/models/processing_error.hpp
 * \brief ProcessingError — the typed error vocabulary shared by the processing pipeline
 *        (Core) and the post-render bookkeeping (Models::ProjectItem).
 *
 * A single error type used wherever a failure is *consumed as a result* rather than as a live
 * log line: it replaces the free-text \c ProcessingOutcome::errorMessage, is returned from
 * \c ProjectItem::applyProcessingResults(), and its \c code / \c category enums also tag the
 * per-input skip carried by \c Core::InputResult. It carries a stable machine \c code (so a
 * consumer can localise / group / branch without parsing English) plus the human \c message and
 * the \c file / \c slice it happened on.
 *
 * Lives in Models — like \c ProcessingSliceRecord and \c AppliedCanvasProfile — so \c ProjectItem
 * can consume it without depending on Core. Core (the pipeline) already depends on Models, so it
 * uses the same type with no layering inversion.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROCESSING_ERROR_HPP
#define PLATEMAKER_MODELS_PROCESSING_ERROR_HPP

#include <string>

#include "platemaker/platemaker_export.h"

namespace Platemaker::Models {

/// Broad phase an error belongs to, so a consumer can group/react without knowing every code.
enum class ProcessingErrorCategory {
    Load,         //!< Reading / decoding / measuring / cropping / scaling an input.
    ProfileMatch, //!< Canvas-profile resolution (reserved — matching is non-fatal today).
    Slice,        //!< Slicing the assembled strip.
    Encode,       //!< Encoding / writing an output slice.
    Io,           //!< Filesystem access outside the above (e.g. hashing a file after render).
    Internal,     //!< An unexpected/unforeseen fault (a bug, a dependency, an out-of-memory) — one for
                  //!< the developer, not the user. The message is a diagnostic to attach to a bug report.
};

/// Stable identifier for a specific failure. Values are additive — never renumber/remove.
enum class ProcessingErrorCode {
    NoPagesLoaded,    //!< Load   — the strip was empty after phase 1 (nothing loaded successfully).
    InputLoadFailed,  //!< Load   — one input's decode / dimensions / crop / scale threw (non-fatal skip).
    SliceEncodeFailed,//!< Encode — ImageIO::save threw while writing a slice (fatal).
    SlicingFailed,    //!< Slice  — strip slicing threw (fatal).
    InputHashFailed,  //!< Io     — an input could not be hashed *after* a successful render
                      //!<          (locked / permission / offline) — the silent-loop case.
    OutputHashFailed, //!< Io     — a saved output slice could not be hashed (missing baseline).
    Unexpected,       //!< Internal — an exception escaped the pipeline's own handling (bug / dependency /
                      //!<            out-of-memory). Caught as a safety net; the message aids a bug report.
};

/**
 * \brief One typed processing failure.
 *
 * The \c message is the human detail (e.g. \c e.what()); it is no longer the primary channel —
 * \c code / \c category are what consumers should branch on. \c file / \c slice give the location
 * (\c file is an input or output path; \c slice an output filename); either may be empty when it
 * does not apply.
 */
struct PLATEMAKER_EXPORT ProcessingError {
    ProcessingErrorCode     code     = ProcessingErrorCode::InputLoadFailed;
    ProcessingErrorCategory category = ProcessingErrorCategory::Load;
    std::string             message; //!< Human-readable detail. Not the machine-facing channel.
    std::string             file;    //!< Input or output path it happened on ("" if n/a).
    std::string             slice;   //!< Output slice filename ("" if n/a).
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROCESSING_ERROR_HPP
