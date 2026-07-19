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
#include <platemaker/infrastructure/file/path_utf8.hpp>

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

// WARNING: these move operations enumerate every member by hand. A new field left out
// of them is silently default-constructed on every move — and since loading a workspace
// moves ProjectItems, the field would arrive empty with nothing to hint why. Add new
// members here and in operator=(ProjectItem&&) as well.
ProjectItem::ProjectItem(ProjectItem&& other) noexcept
    : name(std::move(other.name))
    , uuid(std::move(other.uuid))
    , inputDirectory(std::move(other.inputDirectory))
    , canvasProfileIds(std::move(other.canvasProfileIds))
    , outputProfileId(std::move(other.outputProfileId))
    , outputSignature(std::move(other.outputSignature))
    , canvasProfileIdsAtRender(std::move(other.canvasProfileIdsAtRender))
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
        canvasProfileIds      = std::move(other.canvasProfileIds);
        outputProfileId       = std::move(other.outputProfileId);
        outputSignature       = std::move(other.outputSignature);
        canvasProfileIdsAtRender = std::move(other.canvasProfileIdsAtRender);
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

bool ProjectItem::sanitize(const std::vector<CanvasProfile>& workspaceProfiles)
{
    namespace fs = std::filesystem;

    m_isUpToDate = true;

    for (auto& file : m_input_images) {
        // utf8ToPath() rather than the bare string: on MSVC a narrow path is read in the ANSI
        // code page, so a non-ASCII path would report as Missing even though the file is there.
        if (!fs::exists(Infrastructure::utf8ToPath(file.filePath))) {
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

    // Validate output slices against disk: a deleted or externally-edited
    // output makes the project out-of-date even when every input is unchanged.
    for (auto& out : m_output_images) {
        const std::string path = m_output_directory + "/" + out.fileName;

        if (!fs::exists(Infrastructure::utf8ToPath(path))) {
            out.status   = FileStatus::Missing;
            m_isUpToDate = false;
            continue;
        }

        // No stored hash to compare against (legacy record) — trust existence.
        if (out.sha256.empty()) {
            out.status = FileStatus::Done;
            continue;
        }

        const std::string currentHash =
            Infrastructure::FileMetaData::computeFileSha256(path);

        if (currentHash != out.sha256) {
            out.status   = FileStatus::Modified;
            m_isUpToDate = false;
        } else {
            out.status = FileStatus::Done;
        }
    }

    // Config axis: a canvas-profile edit leaves every file byte-identical, so nothing
    // above can see it. Mark what it invalidated as Desynchronized ("out of sync with
    // config") — the status the UI already renders in amber.
    //
    // Only files the disk pass found clean are re-flagged: Missing / Modified / Pending
    // are more specific and more urgent, so a config change must not mask them.
    const auto markDesynchronized = [](FileStatus& status, FileStatus cleanValue) {
        if (status == cleanValue)
            status = FileStatus::Desynchronized;
    };

    const auto canvasChange = detectCanvasConfigChange(workspaceProfiles);
    if (canvasChange.any()) {
        m_isUpToDate = false;

        if (canvasChange.listChanged) {
            // The effective profile list itself changed (or, for a workspace written
            // before fingerprints existed, there is no baseline at all). Which page got
            // which profile can no longer be attributed, so everything is suspect.
            for (auto& inf : m_input_images)
                markDesynchronized(inf.status, FileStatus::Processed);
            for (auto& out : m_output_images)
                markDesynchronized(out.status, FileStatus::Done);
        } else {
            // Precise: only the pages whose applied profile changed, plus the slices
            // they fed (provenance already records that mapping).
            for (const auto& path : canvasChange.changedInputs) {
                for (auto& inf : m_input_images)
                    if (inf.filePath == path)
                        markDesynchronized(inf.status, FileStatus::Processed);

                for (const auto& outName : outputsForInput(path))
                    for (auto& out : m_output_images)
                        if (out.fileName == outName)
                            markDesynchronized(out.status, FileStatus::Done);
            }
        }
    }

    return m_isUpToDate;
}

// ---------------------------------------------------------------------------
// dirtyOutputNames / inputsAllProcessed
// ---------------------------------------------------------------------------

std::vector<std::string> ProjectItem::dirtyOutputNames() const
{
    std::vector<std::string> names;
    for (const auto& out : m_output_images)
        if (out.status != FileStatus::Done)
            names.push_back(out.fileName);
    return names;
}

bool ProjectItem::inputsAllProcessed() const noexcept
{
    for (const auto& inf : m_input_images)
        if (inf.status != FileStatus::Processed)
            return false;
    return !m_input_images.empty();
}

// ---------------------------------------------------------------------------
// Canvas profile staleness
// ---------------------------------------------------------------------------

std::vector<std::string> ProjectItem::effectiveCanvasProfileIds(
    const std::vector<CanvasProfile>& workspaceProfiles) const
{
    std::vector<std::string> ids;

    if (canvasProfileIds.empty()) {
        // No per-project filter → CanvasProfileMatcher accepts every workspace
        // profile, so the whole palette is what we render with.
        ids.reserve(workspaceProfiles.size());
        for (const auto& cp : workspaceProfiles)
            ids.push_back(cp.id);
        return ids;
    }

    // Keep the project's order, but drop ids that no longer exist in the workspace:
    // they match nothing, so they cannot influence a render either way.
    ids.reserve(canvasProfileIds.size());
    for (const auto& id : canvasProfileIds) {
        const auto it = std::find_if(
            workspaceProfiles.begin(), workspaceProfiles.end(),
            [&id](const CanvasProfile& cp) { return cp.id == id; });
        if (it != workspaceProfiles.end())
            ids.push_back(id);
    }
    return ids;
}

CanvasConfigChange ProjectItem::detectCanvasConfigChange(
    const std::vector<CanvasProfile>& workspaceProfiles) const
{
    CanvasConfigChange change;

    // Never rendered → no baseline to compare against, and the inputs are Pending
    // anyway, which already forces a full run. Note this cannot be inferred from
    // canvasProfileIdsAtRender being empty: a project rendered with no canvas
    // profiles at all legitimately has an empty list, and adding a profile to it
    // later must still register as a change.
    if (m_output_images.empty())
        return change;

    // Workspaces written before per-input fingerprints existed have no baseline
    // either, so a project that uses profiles reports listChanged here and takes one
    // full re-render to establish it. That is the honest outcome — those outputs may
    // genuinely be stale, since not noticing this is exactly the bug being fixed.
    change.listChanged =
        (effectiveCanvasProfileIds(workspaceProfiles) != canvasProfileIdsAtRender);

    // Per-input: did the profile this page was rendered with change content?
    for (const auto& inf : m_input_images) {
        if (inf.canvasProfileId.empty())
            continue;   // no profile applied — the list check above covers this page

        const auto it = std::find_if(
            workspaceProfiles.begin(), workspaceProfiles.end(),
            [&inf](const CanvasProfile& cp) { return cp.id == inf.canvasProfileId; });

        // Profile deleted outright, or its render-relevant fields differ.
        if (it == workspaceProfiles.end() ||
            canvasRenderFingerprint(*it) != inf.canvasFingerprint)
        {
            change.changedInputs.push_back(inf.filePath);
        }
    }

    return change;
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
    const std::vector<AppliedCanvasProfile>&  appliedProfiles,
    const std::vector<CanvasProfile>&         workspaceProfiles,
    const std::string&                        outputDirectory,
    const std::string&                        timestamp)
{
    // Build contributesTo map: filePath → [output file names].
    std::unordered_map<std::string, std::vector<std::string>> contributes;
    for (const auto& rec : records)
        for (const auto& seg : rec.sourceMap)
            contributes[seg.sourceFilePath].push_back(rec.fileName);

    // filePath → canvas profile the run applied to it.
    std::unordered_map<std::string, const AppliedCanvasProfile*> applied;
    applied.reserve(appliedProfiles.size());
    for (const auto& ap : appliedProfiles)
        applied[ap.sourceFilePath] = &ap;

    // Update each InputFile: hash, status, timestamp, contributesTo, canvas baseline.
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

        // Record which profile produced this page, so a later edit to it is detectable.
        // Inputs the run never reached (e.g. Missing) get no entry — clear the baseline
        // rather than leave a stale one claiming a profile was applied.
        const auto ap = applied.find(inf.filePath);
        if (ap != applied.end()) {
            inf.canvasProfileId   = ap->second->profileId;
            inf.canvasFingerprint = ap->second->fingerprint;
        } else {
            inf.canvasProfileId.clear();
            inf.canvasFingerprint.clear();
        }
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

    // Baseline for detectCanvasConfigChange(): the profile list this render used.
    // Captured here rather than by the caller so it can never drift out of sync with
    // the per-input fingerprints written above.
    canvasProfileIdsAtRender = effectiveCanvasProfileIds(workspaceProfiles);

    rebuildLookupTables();
}

// ---------------------------------------------------------------------------
// applyPartialResults
// ---------------------------------------------------------------------------

void ProjectItem::applyPartialResults(
    const std::vector<ProcessingSliceRecord>& records)
{
    // Partial re-render: only the dirty output slices were regenerated. Inputs
    // are unchanged, so their hashes / provenance / contributesTo stay as-is —
    // we just refresh the hash and clear the dirty status of the rewritten
    // outputs. (Unlike applyProcessingResults, which rebuilds everything.)
    for (const auto& rec : records) {
        for (auto& out : m_output_images) {
            if (out.fileName == rec.fileName) {
                out.sha256    = rec.outputSha256;
                out.sourceMap = rec.sourceMap;
                out.status    = FileStatus::Done;
                break;
            }
        }
    }

    // Up-to-date only if no output remains dirty (and inputs were already clean).
    m_isUpToDate = true;
    for (const auto& out : m_output_images) {
        if (out.status != FileStatus::Done) {
            m_isUpToDate = false;
            break;
        }
    }
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

// ---------------------------------------------------------------------------
// addCanvasProfile
// ---------------------------------------------------------------------------

bool ProjectItem::addCanvasProfile(
    const std::vector<CanvasProfile>& workspaceProfiles,
    const std::string&                profileId)
{
    // Idempotent: already linked.
    if (std::find(canvasProfileIds.begin(), canvasProfileIds.end(), profileId)
            != canvasProfileIds.end())
        return true;

    // Locate the profile in the workspace palette.
    const CanvasProfile* adding = nullptr;
    for (const auto& p : workspaceProfiles)
        if (p.id == profileId) { adding = &p; break; }
    if (!adding) return false; // unknown ID

    // Conflict guard (SPECIFICATION.md §7.5.2):
    // Reject if any already-linked profile shares the same canvas dimensions.
    for (const auto& existingId : canvasProfileIds) {
        for (const auto& p : workspaceProfiles) {
            if (p.id == existingId &&
                p.canvasSize.width  == adding->canvasSize.width &&
                p.canvasSize.height == adding->canvasSize.height)
                return false;
        }
    }

    canvasProfileIds.push_back(profileId);
    return true;
}

} // namespace Platemaker::Models
