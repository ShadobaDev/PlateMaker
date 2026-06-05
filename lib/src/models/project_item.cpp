/**
 * \file lib/src/models/project_item.cpp
 * \brief ProjectItem implementation.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/models/project_item.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProjectItem::ProjectItem() noexcept
    : m_isUpToDate(false)
{}

ProjectItem::ProjectItem(ProjectItem&& other) noexcept
    : name(std::move(other.name))
    , uuid(std::move(other.uuid))
    , inputDirectory(std::move(other.inputDirectory))
    , m_input_images(std::move(other.m_input_images))
    , m_output_images(std::move(other.m_output_images))
    , m_output_directory(std::move(other.m_output_directory))
    , m_isUpToDate(other.m_isUpToDate)
    , m_inputToOutputLookup(std::move(other.m_inputToOutputLookup))
    , m_sha256Index(std::move(other.m_sha256Index))
{}

ProjectItem& ProjectItem::operator=(ProjectItem&& other) noexcept
{
    if (this != &other) {
        name                  = std::move(other.name);
        uuid                  = std::move(other.uuid);
        inputDirectory        = std::move(other.inputDirectory);
        m_input_images        = std::move(other.m_input_images);
        m_output_images       = std::move(other.m_output_images);
        m_output_directory    = std::move(other.m_output_directory);
        m_isUpToDate          = other.m_isUpToDate;
        m_inputToOutputLookup = std::move(other.m_inputToOutputLookup);
        m_sha256Index         = std::move(other.m_sha256Index);
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::vector<InputFile>& ProjectItem::getInputImages() noexcept
{
    return m_input_images;
}

const std::vector<InputFile>& ProjectItem::getInputImages() const noexcept
{
    return m_input_images;
}

std::vector<OutputFile>& ProjectItem::getOutputImages() noexcept
{
    return m_output_images;
}

const std::vector<OutputFile>& ProjectItem::getOutputImages() const noexcept
{
    return m_output_images;
}

std::string& ProjectItem::getOutputDirectory() noexcept
{
    return m_output_directory;
}

const std::string& ProjectItem::getOutputDirectory() const noexcept
{
    return m_output_directory;
}

bool ProjectItem::isUpToDate() const noexcept
{
    return m_isUpToDate;
}

const std::vector<std::string>& ProjectItem::outputsForInput(
    const std::string& filePath) const noexcept
{
    static const std::vector<std::string> empty;
    const auto it = m_inputToOutputLookup.find(filePath);
    return (it != m_inputToOutputLookup.end()) ? it->second : empty;
}

// ---------------------------------------------------------------------------
// sanitize
// ---------------------------------------------------------------------------

bool ProjectItem::sanitize()
{
    namespace fs = std::filesystem;

    m_isUpToDate = true;

    for (auto& file : m_input_images) {
        if (!fs::exists(file.filePath)) {
            file.status  = FileStatus::Missing;
            m_isUpToDate = false;
            continue;
        }

        if (file.sha256.empty()) {
            // Never processed — mark as Pending so the pipeline knows to run.
            file.status  = FileStatus::Pending;
            m_isUpToDate = false;
            continue;
        }

        const std::string currentHash =
            Infrastructure::FileMetaData::computeFileSha256(file.filePath);

        if (currentHash != file.sha256) {
            file.status  = FileStatus::Modified;
            m_isUpToDate = false;
        } else {
            file.status = FileStatus::Processed;
        }
    }

    return m_isUpToDate;
}

// ---------------------------------------------------------------------------
// rebuildLookupTables
// ---------------------------------------------------------------------------

void ProjectItem::rebuildLookupTables()
{
    m_inputToOutputLookup.clear();
    m_sha256Index.clear();

    for (const auto& inf : m_input_images) {
        // Map filePath → [output file names it contributed to].
        m_inputToOutputLookup[inf.filePath] = inf.contributesTo;

        // Map sha256 → filePath (for rename detection).
        if (!inf.sha256.empty())
            m_sha256Index[inf.sha256] = inf.filePath;
    }
}

// ---------------------------------------------------------------------------
// applyProcessingResults
// ---------------------------------------------------------------------------

void ProjectItem::applyProcessingResults(
    const std::vector<ProcessingSliceRecord>& records,
    const std::string&                        outputDirectory,
    const std::string&                        timestamp)
{
    // Build contributesTo map: filePath → [output file names].
    std::unordered_map<std::string, std::vector<std::string>> contributes;
    for (const auto& rec : records)
        for (const auto& seg : rec.sourceMap)
            contributes[seg.sourceFilePath].push_back(rec.fileName);

    // Update each InputFile: hash, status, timestamp, contributesTo.
    for (auto& inf : m_input_images) {
        const std::string h =
            Infrastructure::FileMetaData::computeFileSha256(inf.filePath);
        if (!h.empty()) {
            inf.sha256        = h;
            inf.status        = FileStatus::Processed;
            inf.lastProcessed = timestamp;
        }
        const auto it = contributes.find(inf.filePath);
        inf.contributesTo = (it != contributes.end())
                            ? it->second
                            : std::vector<std::string>{};
    }

    // Rebuild OutputFile list from records.
    m_output_images.clear();
    m_output_images.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        OutputFile outf;
        outf.uuid      = "out-" + std::to_string(i);
        outf.fileName  = records[i].fileName;
        outf.sha256    = records[i].outputSha256;
        outf.sourceMap = records[i].sourceMap;
        outf.status    = FileStatus::Done;
        m_output_images.push_back(std::move(outf));
    }

    m_output_directory = outputDirectory;
    m_isUpToDate       = true;

    rebuildLookupTables();
}

// ---------------------------------------------------------------------------
// mergeFileScan
// ---------------------------------------------------------------------------

ScanMergeResult ProjectItem::mergeFileScan(
    const std::vector<std::string>& newFilePaths)
{
    ScanMergeResult result;

    // -------------------------------------------------------------------
    // Build lookup maps from the existing input list.
    // -------------------------------------------------------------------

    // path → index in m_input_images (for path-based matching).
    std::unordered_map<std::string, std::size_t> pathToOldIdx;
    pathToOldIdx.reserve(m_input_images.size());
    for (std::size_t i = 0; i < m_input_images.size(); ++i)
        pathToOldIdx[m_input_images[i].filePath] = i;

    // sha256 → index in m_input_images (for rename detection).
    // Only populate for files that have been processed (non-empty sha256).
    std::unordered_map<std::string, std::size_t> sha256ToOldIdx;
    sha256ToOldIdx.reserve(m_input_images.size());
    for (std::size_t i = 0; i < m_input_images.size(); ++i) {
        const auto& inf = m_input_images[i];
        if (!inf.sha256.empty())
            sha256ToOldIdx[inf.sha256] = i;
    }

    // Set of new paths for fast "is the old file still here?" check.
    std::unordered_set<std::string> newPathSet(
        newFilePaths.begin(), newFilePaths.end());

    // -------------------------------------------------------------------
    // Build the new ordered list.
    // -------------------------------------------------------------------
    std::vector<InputFile> newList;
    newList.reserve(newFilePaths.size());

    // Track which old indices were consumed (to find removals later).
    std::unordered_set<std::size_t> consumedOldIndices;

    bool structuralChange = false;

    for (int newOrder = 0; newOrder < static_cast<int>(newFilePaths.size()); ++newOrder) {
        const std::string& newPath = newFilePaths[static_cast<std::size_t>(newOrder)];

        // --- Case 1: same path exists in old list ---
        const auto pathIt = pathToOldIdx.find(newPath);
        if (pathIt != pathToOldIdx.end()) {
            InputFile inf = m_input_images[pathIt->second];
            if (inf.order != newOrder) structuralChange = true;
            inf.order = newOrder;
            consumedOldIndices.insert(pathIt->second);
            newList.push_back(std::move(inf));
            continue;
        }

        // --- Case 2: rename detection — hash the new file ---
        const std::string newHash =
            Infrastructure::FileMetaData::computeFileSha256(newPath);

        // A sha256 match is only a rename if:
        //   (a) the hash is non-empty,
        //   (b) the hash exists in the old records, AND
        //   (c) that old record has NOT already been claimed by a path match
        //       (guards against two files with identical content being
        //        incorrectly treated as renames of the same source).
        const auto sha256It = (!newHash.empty())
                              ? sha256ToOldIdx.find(newHash)
                              : sha256ToOldIdx.end();

        const bool isRename = sha256It != sha256ToOldIdx.end() &&
                              consumedOldIndices.count(sha256It->second) == 0;

        if (isRename) {
            // Same content, new path → rename.
            const std::size_t oldIdx = sha256It->second;
            InputFile inf = m_input_images[oldIdx];
            result.renamed.push_back(newPath);
            inf.filePath = newPath; // update path, keep everything else
            // If the strip position changed, that's a structural change.
            if (inf.order != newOrder) structuralChange = true;
            inf.order = newOrder;
            consumedOldIndices.insert(oldIdx);
            newList.push_back(std::move(inf));
        } else {
            // --- Case 3: brand-new file ---
            InputFile inf;
            inf.uuid     = "file-" + std::to_string(newList.size());
            inf.filePath = newPath;
            inf.order    = newOrder;
            inf.status   = FileStatus::Pending;
            newList.push_back(std::move(inf));
            result.added.push_back(newPath);
            structuralChange = true;
        }
    }

    // -------------------------------------------------------------------
    // Detect removed files (old indices not consumed and path not in newPaths).
    // -------------------------------------------------------------------
    for (std::size_t i = 0; i < m_input_images.size(); ++i) {
        if (consumedOldIndices.count(i) == 0) {
            result.removed.push_back(m_input_images[i].filePath);
            structuralChange = true;
        }
    }

    // -------------------------------------------------------------------
    // Apply structural change consequences.
    // -------------------------------------------------------------------
    if (structuralChange) {
        for (auto& outf : m_output_images)
            outf.status = FileStatus::Desynchronized;
        result.outputsInvalidated = true;
    }

    // Replace the old input list with the new one.
    m_input_images = std::move(newList);
    m_isUpToDate   = false; // always requires at least a sanitize() pass

    rebuildLookupTables();
    return result;
}

} // namespace Platemaker::Models
