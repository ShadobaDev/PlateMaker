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
 * ## Lookup tables (non-serialised)
 *
 * \c ProjectItem maintains two runtime lookup tables that are rebuilt from
 * serialised data on every load (\c rebuildLookupTables()):
 *  - \c m_inputToOutputLookup — maps input file path → output file names.
 *  - \c m_sha256Index         — maps SHA-256 hex digest → current file path;
 *                               used to detect file renames across directory scans.
 *
 * ## Incremental processing
 *
 * Call \c sanitize() before running the pipeline to update all file statuses.
 * After a successful pipeline run call \c applyProcessingResults() to update
 * hashes, re-populate provenance and rebuild the lookup tables — all in one
 * place rather than scattered across the CLI/GUI layer.
 *
 * ## Directory re-scan (project mod --input)
 *
 * \c mergeFileScan() replaces the current input list with a new directory
 * scan while preserving incremental-processing data for unchanged files.
 * It uses SHA-256 to identify files that were renamed (same content, different
 * path) and only marks structurally new/removed/reordered files as dirty,
 * avoiding a full reprocess when only filenames changed.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
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
#include "platemaker/platemaker_export.h"
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
 * \c SliceResult or \c OutputFile.  This provenance information drives
 * incremental processing.
 */
struct PLATEMAKER_EXPORT SourceSegment {
    std::string sourceFilePath; //!< Absolute path of the scaled source image.
    int         srcY   = 0;     //!< Y offset within the scaled source where this segment starts.
    int         height = 0;     //!< Number of pixels taken from this source for this slice.
};

/**
 * \brief An input image file tracked by a \c ProjectItem.
 */
struct PLATEMAKER_EXPORT InputFile {
    std::string uuid;                    //!< Unique identifier for this entry.
    std::string filePath;                //!< Absolute path on disk.
    std::string sha256;                  //!< SHA-256 hex digest from the last processing run.
    int         order        = 0;        //!< 0-based position in the virtual strip.
    std::string thumbnailPath;           //!< Path inside `.platemaker-cache/` (not persisted by convention).
    FileStatus  status       = FileStatus::Pending; //!< Current lifecycle status.
    std::string lastProcessed;           //!< ISO 8601 timestamp of the last processing run.
    std::vector<std::string> contributesTo; //!< Output file names produced from this input.
};

/**
 * \brief An output slice file produced by a \c ProjectItem processing run.
 */
struct PLATEMAKER_EXPORT OutputFile {
    std::string uuid;     //!< Unique identifier for this entry.
    std::string fileName; //!< Filename only (e.g. "output_001.png").
    std::string sha256;   //!< SHA-256 hex digest of the generated file.
    std::vector<SourceSegment> sourceMap; //!< Input contributions for this slice.
    FileStatus  status = FileStatus::Done; //!< Current lifecycle status.
};

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
struct PLATEMAKER_EXPORT ProcessingSliceRecord {
    std::string                fileName;     //!< Output filename, e.g. "output_001.png".
    std::string                outputSha256; //!< SHA-256 of the saved output file (may be empty).
    std::vector<SourceSegment> sourceMap;    //!< Provenance: one entry per contributing source file.
};

// ---------------------------------------------------------------------------
// ScanMergeResult
// ---------------------------------------------------------------------------

/**
 * \brief Result returned by \c ProjectItem::mergeFileScan().
 *
 * Summarises the changes detected when a new directory scan is merged into
 * the existing input file list.
 */
struct PLATEMAKER_EXPORT ScanMergeResult {
    std::vector<std::string> added;              //!< Absolute paths of newly added files.
    std::vector<std::string> renamed;            //!< Absolute paths of renamed files (same content).
    std::vector<std::string> removed;            //!< Absolute paths of files no longer present.
    bool outputsInvalidated = false;             //!< True when outputs require a full reprocess.
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
 * ### Typical usage (incremental processing)
 * \code
 * project.sanitize();                       // refresh file statuses
 * if (!project.isUpToDate()) {
 *     auto slices = pipeline.run(project);  // Core layer
 *     project.applyProcessingResults(records, outDir, now);
 * }
 * \endcode
 *
 * \note The non-serialised lookup tables (\c m_inputToOutputLookup and
 *       \c m_sha256Index) are NOT written to JSON.  They must be rebuilt
 *       after every deserialisation via \c rebuildLookupTables().
 */
class PLATEMAKER_EXPORT ProjectItem {
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

    /// Move constructor — transfers ownership and runtime lookup tables.
    ProjectItem(ProjectItem&& other) noexcept;

    /// Move assignment — transfers ownership and runtime lookup tables.
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
     * \brief Returns \c true when all input files are \c Processed and their
     *        SHA-256 hashes match the stored values.
     *
     * Only valid after calling \c sanitize().
     */
    [[nodiscard]] bool isUpToDate() const noexcept;

    /**
     * \brief Returns the output file names that were produced from \p filePath.
     *
     * Uses the runtime lookup table built by \c rebuildLookupTables() /
     * \c applyProcessingResults().  Returns an empty vector if the file is
     * unknown or was not yet processed.
     *
     * \param filePath Absolute path of the input file.
     * \return Const reference to the list of output file names.
     */
    [[nodiscard]] const std::vector<std::string>& outputsForInput(
        const std::string& filePath) const noexcept;

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

    /**
     * \brief Rebuilds the non-serialised runtime lookup tables from the
     *        current \c m_input_images and \c m_output_images data.
     *
     * Must be called:
     *  - After every deserialisation (e.g. in \c WorkspaceSerializer::load()).
     *  - After \c applyProcessingResults() (called internally).
     *  - After \c mergeFileScan() (called internally).
     *
     * Rebuilds:
     *  - \c m_inputToOutputLookup — filePath → output file names.
     *  - \c m_sha256Index         — sha256   → current file path.
     */
    void rebuildLookupTables();

    /**
     * \brief Applies the results of a pipeline run to all tracked records.
     *
     * Updates each \c InputFile with its current SHA-256 hash, sets status
     * to \c Processed, fills \c contributesTo from the provenance data in
     * \p records, rebuilds the \c OutputFile list and the runtime lookup
     * tables.
     *
     * This method centralises all post-processing bookkeeping that was
     * previously scattered across the CLI layer.
     *
     * \param records        One record per saved output file, in order.
     * \param outputDirectory Absolute path where output files were written.
     * \param timestamp      ISO 8601 string for \c InputFile::lastProcessed.
     */
    void applyProcessingResults(
        const std::vector<ProcessingSliceRecord>& records,
        const std::string&                        outputDirectory,
        const std::string&                        timestamp);

    /**
     * \brief Merges a new directory scan into the current input file list.
     *
     * Replaces \c m_input_images with a new ordered list derived from
     * \p newFilePaths while maximally preserving existing incremental-
     * processing data:
     *
     * - Files matched **by path**: existing record is kept as-is.
     * - Files matched **by SHA-256** (same content, new path): treated as a
     *   rename; path is updated but status / hash / contributesTo are kept.
     *   No output invalidation is triggered for pure renames at the same
     *   strip position.
     * - Files with no match: inserted as new \c Pending entries.
     * - Old files no longer present: removed.
     *
     * If any structural change is detected (file added, removed, or
     * reordered at a different strip position), all \c OutputFile entries are
     * marked \c Desynchronized and \c ScanMergeResult::outputsInvalidated is
     * set to \c true.
     *
     * \param newFilePaths Absolute paths from the new directory scan, in
     *                     the desired strip order (typically sorted by name).
     * \return A \c ScanMergeResult describing what changed.
     */
    ScanMergeResult mergeFileScan(const std::vector<std::string>& newFilePaths);

private:
    std::vector<InputFile>  m_input_images;     //!< Ordered input image list.
    std::vector<OutputFile> m_output_images;    //!< Output slice list from last run.
    std::string             m_output_directory; //!< Absolute path for output slices.

    bool m_isUpToDate = false; //!< Updated by sanitize() / applyProcessingResults().

    // -----------------------------------------------------------------------
    // Runtime lookup tables (NOT serialised)
    // -----------------------------------------------------------------------

    /**
     * \brief Maps input file path → output file names it contributed to.
     *
     * Rebuilt by \c rebuildLookupTables().  Not written to JSON.
     */
    std::unordered_map<std::string, std::vector<std::string>> m_inputToOutputLookup;

    /**
     * \brief Maps SHA-256 hex digest → current absolute file path.
     *
     * Used in \c mergeFileScan() to detect renames (file with known hash
     * has moved to a new path).  Not written to JSON.
     */
    std::unordered_map<std::string, std::string> m_sha256Index;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_ITEM_HPP
