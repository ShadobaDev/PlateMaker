/**
 * \file lib/include/platemaker/models/project_files.hpp
 * \brief The files a ProjectItem tracks - its inputs, its outputs, and their lifecycle status.
 *
 * Split out of \c project_item.hpp so that header can describe the project itself.  These four
 * types are consumed independently all over the CLI and GUI - an input tile reads an
 * \c InputFile, an output tile an \c OutputFile - so they are not private helpers of
 * \c ProjectItem, and having to include the whole 700-line project header to name one was the
 * tell.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROJECT_FILES_HPP
#define PLATEMAKER_MODELS_PROJECT_FILES_HPP

#include <string>
#include <vector>

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/**
 * \enum FileStatus
 * \brief Lifecycle status of a file tracked by a \c ProjectItem.
 */
enum class FileStatus {
    Pending,        //!< New file, not yet processed.
    Processed,      //!< File processed and its hash matches the stored hash.
    Modified,       //!< File exists on disk but its content has changed since last processing.
    Missing,        //!< File was registered but cannot be found on disk.
    Desynchronized, //!< Output is out-of-sync with the current input set.
    Done,           //!< Output slice is up-to-date with the current input set and workspace config.
    Skipped,        //!< Input the last render could not include: the file is missing or failed to
                    //!< load. A size mismatch is NOT a skip — an unmatched page is rendered without
                    //!< margins and ends up Processed with an empty canvasProfileId. Sticky across
                    //!< sanitize() while the file is unchanged. Distinct from Missing, which is a
                    //!< pre-render disk check, and from Pending, which was never rendered.
    Error           //!< Input was rendered, but its content hash could not be computed afterwards
                    //!< (locked file / denied permission / offline drive). The output exists; the
                    //!< verification baseline does not. Sticky across sanitize() like Skipped and it
                    //!< does NOT force a re-render — otherwise the project would silently reprocess
                    //!< everything on every run forever. Recovers to Processed once the file hashes.
};

// ---------------------------------------------------------------------------
// Project data types
// ---------------------------------------------------------------------------

/**
 * \brief Describes the contribution of one source file to a single output slice.
 *
 * One \c SourceSegment is stored per source image that contributed pixels to a
 * \c SliceResult or \c OutputFile.  This provenance information drives
 * incremental processing.
 */
struct SourceSegment {
    std::string sourceFilePath; //!< Absolute path of the scaled source image.
    int         sourceY = 0;    //!< Y offset within the scaled source where this segment starts.
    int         height = 0;     //!< Number of pixels taken from this source for this slice.
};

/**
 * \brief An input image file tracked by a \c ProjectItem.
 */
struct InputFile {
    std::string uid;                     //!< Local unique id for this entry (e.g. "file-<hex>"). Not an RFC 4122 UUID.
    std::string filePath;                //!< Absolute path on disk.
    std::string sha256;                  //!< SHA-256 hex digest from the last processing run.
    int         order        = 0;        //!< 0-based position in the virtual strip.
    std::string thumbnailPath;           //!< Path inside `.platemaker-cache/` (not persisted by convention).
    FileStatus  status       = FileStatus::Pending; //!< Current lifecycle status.
    std::string lastProcessed;           //!< ISO 8601 timestamp of the last processing run.
    std::vector<std::string> contributesTo; //!< Output file names produced from this input.

    /**
     * \brief Id of the canvas profile applied to this page at the last render.
     *
     * Empty means no profile matched (the page was skipped) or the project has no
     * canvas profiles at all.  Not needed for staleness detection — \c canvasFingerprint
     * already catches a profile swap — but kept so the UI can say *which* profile was
     * used instead of showing an opaque fingerprint.
     */
    std::string canvasProfileId;

    /**
     * \brief \c canvasRenderFingerprint() of the profile applied at the last render.
     *
     * The record of what was actually applied to *this* page, exactly like \c sha256
     * records its content.  Compared against the current profile on the next run: a
     * mismatch means this page's output is stale even though its file never changed.
     * Empty when no profile was applied.
     */
    std::string canvasFingerprint;

    /**
     * \brief Display dimensions of this page at the last render, in pixels (0 = unknown).
     *
     * Recorded in the same coordinate space canvas matching uses — post-autorot display W×H,
     * exactly the values the pipeline fed to \c CanvasProfileMatcher::resolveForSize().  Canvas profiles
     * match purely by W×H, so storing them is what lets \c detectCanvasConfigChange() answer
     * *offline* "which profile would this page match now?" instead of blanket-invalidating the whole
     * project whenever the effective profile list changes.  Both zero means the page has not been
     * rendered since dimensions were tracked (legacy record) — the caller may backfill from the file
     * header, and staleness detection falls back to the coarse list comparison until it does.
     */
    int width  = 0;
    int height = 0;
};

/**
 * \brief An output slice file produced by a \c ProjectItem processing run.
 */
struct OutputFile {
    std::string uid;      //!< Local unique id for this entry (e.g. "out-3"). Not an RFC 4122 UUID.
    std::string fileName; //!< Filename only (e.g. "output_001.png").
    std::string sha256;   //!< SHA-256 hex digest of the generated file.
    std::vector<SourceSegment> sourceMap; //!< Input contributions for this slice.
    FileStatus  status = FileStatus::Done; //!< Current lifecycle status.
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_FILES_HPP
