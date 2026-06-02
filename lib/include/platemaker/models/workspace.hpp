/**
 * \file lib/include/platemaker/models/workspace.hpp
 * \brief Workspace data model — the root document type persisted as a .platemaker.json file.
 *
 * Also contains ProcessedFileRecord, the per-file incremental-processing cache entry.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_WORKSPACE_HPP
#define PLATEMAKER_MODELS_WORKSPACE_HPP

#include <string>
#include <vector>

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/page_item.hpp>

namespace Platemaker::Models {

/**
 * \brief Incremental processing cache entry for a single input file.
 *
 * Persisted inside \c Workspace::processedFiles.  On the next run the pipeline
 * compares the current SHA-256 of each source file against this record; files
 * whose hash is unchanged do not need to be reprocessed.
 */
struct ProcessedFileRecord {
    std::string              inputFilePath; //!< Absolute path of the source image at the time of processing.
    std::string              sha256;        //!< Hex-encoded SHA-256 digest of the file contents.
    std::string              lastProcessed; //!< ISO 8601 timestamp of the last successful processing run.
    std::vector<std::string> contributesTo; //!< Output filenames (e.g. "output_003.png") that contain pixels from this input.
};

/**
 * \class Workspace
 * \brief The root document that is serialised to and deserialised from
 *        \c <project-name>.platemaker.json.
 *
 * A workspace holds all project settings, the ordered page list, profile
 * definitions, and the incremental-processing cache.  It is the single source
 * of truth passed between all components of libplatemaker.
 *
 * \note The \c version field drives the migration chain in WorkspaceSerializer.
 *       Increment it whenever the serialised schema changes in a breaking way.
 *
 * \warning \c stripDirty must be set to \c true whenever the page order changes,
 *          a page is added or removed, or the active OutputProfile changes.  When
 *          \c true the entire strip is reprocessed regardless of individual file
 *          hashes.
 */
class Workspace {
public:
    /**
     * \brief Schema version number.
     *
     * Start at 1; increment on each breaking schema change.  WorkspaceSerializer
     * uses this to run the appropriate migration chain when loading older files.
     */
    int version = 1;

    std::vector<CanvasProfile> canvasProfiles;  //!< All defined canvas profiles.
    std::vector<OutputProfile> outputProfiles;  //!< All defined output profiles.

    std::string activeCanvasProfileName; //!< Name of the currently selected CanvasProfile.
    std::string activeOutputProfileName; //!< Name of the currently selected OutputProfile.

    std::vector<PageItem> pages; //!< Ordered list of source image files forming the virtual strip.

    std::string outputDirectory; //!< Absolute path to the directory where output slices are written.

    /**
     * \brief Incremental processing cache — one entry per source file that has been processed.
     *
     * On each run the pipeline compares current file hashes against these records
     * to determine which outputs are still valid.
     */
    std::vector<ProcessedFileRecord> processedFiles;

    /**
     * \brief Indicates that the entire strip must be reprocessed on the next run.
     *
     * Set to \c true automatically by the workspace management layer whenever the
     * strip composition changes (page added, removed, reordered, or OutputProfile
     * modified).  Reset to \c false after a successful full reprocess.
     */
    bool stripDirty = false;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_WORKSPACE_HPP
