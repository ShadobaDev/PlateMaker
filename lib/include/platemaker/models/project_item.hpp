/**
 * \file lib/include/platemaker/models/project_item.hpp
 * \brief ProjectItem — represents a single comic chapter (project).
 *
 * A \c ProjectItem is the central unit of work in Platemaker.  It aggregates:
 *  - An ordered list of input image files (\c InputFile) that form the virtual
 *    strip for one comic chapter.
 *  - A list of output slice files (\c OutputFile) produced by the last
 *    processing run.
 *  - Metadata required for incremental processing (SHA-256 hashes, statuses).
 *
 * \c sanitize() walks the filesystem and updates file statuses so the caller
 * can decide whether a full reprocess is needed.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-04
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROJECT_ITEM_HPP
#define PLATEMAKER_MODELS_PROJECT_ITEM_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
    Done            //!< Output slice is up-to-date with the current input set and workspace config.
};

// ---------------------------------------------------------------------------
// Project data types
// ---------------------------------------------------------------------------

/**
 * \brief Describes the contribution of one source file to a single output slice.
 *
 * One \c SourceSegment is stored per source image that contributed pixels to a
 * \c SliceResult.  This provenance information drives incremental processing.
 */
struct SourceSegment {
    std::string sourceFilePath; //!< Absolute path of the scaled source image.
    int         srcY   = 0;     //!< Y offset within the scaled source where this segment starts.
    int         height = 0;     //!< Number of pixels taken from this source for this slice.
};

/**
 * \brief An input image file tracked by a \c ProjectItem.
 */
struct InputFile {
    std::string uuid;                    //!< Unique identifier for this entry.
    std::string filePath;                //!< Absolute path on disk.
    std::string sha256;                  //!< SHA-256 hex digest from the last processing run.
    int         order        = 0;        //!< 0-based position in the virtual strip.
    std::string thumbnailPath;           //!< Path inside `.platemaker-cache/` (not persisted).
    FileStatus  status       = FileStatus::Pending; //!< Current lifecycle status.
    std::string lastProcessed;           //!< ISO 8601 timestamp of the last processing run.
    std::vector<std::string> contributesTo; //!< Output file names produced from this input.
};

/**
 * \brief An output slice file produced by a \c ProjectItem processing run.
 */
struct OutputFile {
    std::string uuid;     //!< Unique identifier for this entry.
    std::string fileName; //!< Filename only (e.g. "output_001.png").
    std::string sha256;   //!< SHA-256 hex digest of the generated file.
    std::vector<SourceSegment> sourceMap; //!< Input contributions for this slice.
    FileStatus  status = FileStatus::Done; //!< Current lifecycle status.
};

// ---------------------------------------------------------------------------
// ProjectItem class
// ---------------------------------------------------------------------------

/**
 * \class ProjectItem
 * \brief Represents a single comic chapter — the primary unit of work.
 *
 * \c ProjectItem is **move-only** to prevent accidental copies of large
 * collections.  Use \c std::move() when passing to containers.
 *
 * Call \c sanitize() before running the pipeline to update all file statuses
 * and determine whether a reprocess is needed (\c isUpToDate() returns false
 * when any input is new, modified, or missing).
 */
class ProjectItem {
public:
    // -----------------------------------------------------------------------
    // Public serialised fields
    // -----------------------------------------------------------------------
    std::string name;           //!< Human-readable chapter name (e.g. "Chapter 01").
    std::string uuid;           //!< Auto-generated unique identifier.
    std::string inputDirectory; //!< Absolute path to the source image directory.

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Default constructor — creates an empty, unprocessed project.
    ProjectItem() noexcept;

    ~ProjectItem() = default;

    ProjectItem(const ProjectItem&)            = delete;
    ProjectItem& operator=(const ProjectItem&) = delete;

    /// Move constructor — transfers ownership.
    ProjectItem(ProjectItem&& other) noexcept;

    /// Move assignment — transfers ownership.
    ProjectItem& operator=(ProjectItem&& other) noexcept;

    // -----------------------------------------------------------------------
    // Accessors (const + non-const overloads for nlohmann/json compatibility)
    // -----------------------------------------------------------------------

    /**
     * \brief Returns the list of input files.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::vector<InputFile>& getInputImages() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::vector<InputFile>& getInputImages() const noexcept;

    /**
     * \brief Returns the list of output files.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::vector<OutputFile>& getOutputImages() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::vector<OutputFile>& getOutputImages() const noexcept;

    /**
     * \brief Returns the output directory path.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::string& getOutputDirectory() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::string& getOutputDirectory() const noexcept;

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    /**
     * \brief Returns \c true when all input files are \c Processed and match
     *        their stored SHA-256 hashes.
     *
     * Only valid after calling \c sanitize().
     */
    [[nodiscard]] bool isUpToDate() const noexcept;

    // -----------------------------------------------------------------------
    // Operations
    // -----------------------------------------------------------------------

    /**
     * \brief Walks the filesystem and updates the status of every tracked file.
     *
     * For each \c InputFile:
     * - \c Missing   — file no longer exists on disk.
     * - \c Modified  — file exists but its SHA-256 differs from the stored hash.
     * - \c Processed — file exists and its SHA-256 matches.
     * - \c Pending   — file exists but has never been hashed (empty sha256).
     *
     * Sets the internal up-to-date flag to \c false if any file is not
     * \c Processed.
     *
     * \return \c true if every input file is \c Processed and unmodified.
     */
    bool sanitize();

private:
    std::vector<InputFile>  m_input_images;    //!< Ordered input image list.
    std::vector<OutputFile> m_output_images;   //!< Output slice list from last run.
    std::string             m_output_directory;//!< Absolute path for output slices.

    bool m_isUpToDate = false; //!< Updated by sanitize().
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_ITEM_HPP
