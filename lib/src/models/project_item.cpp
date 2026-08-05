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
#include <platemaker/infrastructure/id_generator/id_generator.hpp>

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
    // Order matches member declaration order (the two m_*ProfileId* link fields are now private,
    // declared after outputSignature / canvasProfileIdsAtRender) — keeps -Wreorder quiet.
    : name(std::move(other.name))
    , uid(std::move(other.uid))
    , inputDirectory(std::move(other.inputDirectory))
    , outputSignature(std::move(other.outputSignature))
    , canvasProfileIdsAtRender(std::move(other.canvasProfileIdsAtRender))
    , inputOrderAtRender(std::move(other.inputOrderAtRender))
    , m_canvasProfileIds(std::move(other.m_canvasProfileIds))
    , m_outputProfileId(std::move(other.m_outputProfileId))
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
        uid                   = std::move(other.uid);
        inputDirectory        = std::move(other.inputDirectory);
        outputSignature       = std::move(other.outputSignature);
        canvasProfileIdsAtRender = std::move(other.canvasProfileIdsAtRender);
        inputOrderAtRender    = std::move(other.inputOrderAtRender);
        m_canvasProfileIds    = std::move(other.m_canvasProfileIds);
        m_outputProfileId     = std::move(other.m_outputProfileId);
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

std::vector<InputFile> ProjectItem::inputsInOrder() const
{
    std::vector<InputFile> ordered = m_input_images;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const InputFile& a, const InputFile& b) { return a.order < b.order; });
    return ordered;
}

std::vector<std::string> ProjectItem::orderedInputUids() const
{
    std::vector<const InputFile*> ptrs;
    ptrs.reserve(m_input_images.size());
    for (const auto& f : m_input_images)
        ptrs.push_back(&f);
    std::stable_sort(ptrs.begin(), ptrs.end(),
                     [](const InputFile* a, const InputFile* b) { return a->order < b->order; });

    std::vector<std::string> uids;
    uids.reserve(ptrs.size());
    for (const auto* p : ptrs)
        uids.push_back(p->uid);
    return uids;
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

        // An input the last render produced output for but could not hash afterwards (locked /
        // permission / offline) stays Error until the file becomes readable again. Try to hash now:
        // success means access was restored → recover to Processed and adopt the current content as the
        // verification baseline; failure means still unreachable → remain Error. Like Skipped, Error
        // has an empty sha256 and does NOT mark the project out of date — that is exactly what stops the
        // silent re-render loop (falling through to the empty-sha256 branch below would re-mark it
        // Pending and reprocess everything forever).
        if (file.status == FileStatus::Error) {
            const std::string h =
                Infrastructure::FileMetaData::computeFileSha256(file.filePath);
            if (h.empty())
                continue; // still unreadable — stay Error, do not force a re-render
            file.sha256 = h;
            file.status = FileStatus::Processed;
            continue;
        }

        // A page the last render skipped (no matching / linked canvas profile) stays Skipped as long
        // as the file is unchanged.  sanitize() is disk-based and cannot re-derive the profile match,
        // so it must NOT silently "un-skip" the page to Processed/Pending — doing so lost the render's
        // result on every reopen, and made the project look permanently out of date (a never-rendered
        // skipped page has no hash → Pending → forces a re-render forever, even with every output Done).
        // Skipped is terminal: nothing more can be rendered for it without a profile change, so it does
        // NOT mark the project out of date.  A canvas-profile *list* change is caught separately by
        // detectCanvasConfigChange() below (listChanged), which forces a full re-render and
        // re-evaluates every page — so a page becoming matchable is not missed.
        if (file.status == FileStatus::Skipped) {
            if (file.sha256.empty())
                continue; // no content baseline, but still deliberately skipped — leave it
            if (Infrastructure::FileMetaData::computeFileSha256(file.filePath) == file.sha256)
                continue; // unchanged → remain Skipped
            // The file changed since it was skipped: fall through so it becomes Modified and the next
            // render re-includes or re-skips it.
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

    // Composition axis: reordering / adding / removing inputs shifts the continuous strip, so every
    // downstream slice changes while each input file stays byte-identical — no hash notices. The strip
    // is one concatenation, so a change anywhere invalidates the whole output; mark every clean output
    // Desynchronized (the inputs themselves are unchanged and stay Processed). See
    // detectInputCompositionChange().
    if (detectInputCompositionChange()) {
        m_isUpToDate = false;
        for (auto& out : m_output_images)
            markDesynchronized(out.status, FileStatus::Done);
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
    // Skipped counts as settled: a page with no matching canvas profile is not going to be rendered,
    // so it must not block the "inputs are clean → a partial re-render of dirty outputs suffices"
    // decision (otherwise a project with one permanently-skipped page could never take the partial path).
    // Error counts as settled too: the page was rendered but is unverifiable (locked / offline); it must
    // not force a full re-render, or the silent loop this status exists to break would return.
    for (const auto& inf : m_input_images)
        if (inf.status != FileStatus::Processed &&
            inf.status != FileStatus::Skipped   &&
            inf.status != FileStatus::Error)
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

    if (m_canvasProfileIds.empty()) {
        // No per-project filter → CanvasProfileMatcher accepts every workspace
        // profile, so the whole palette is what we render with.
        ids.reserve(workspaceProfiles.size());
        for (const auto& cp : workspaceProfiles)
            ids.push_back(cp.id);
        return ids;
    }

    // Keep the project's order, but drop ids that no longer exist in the workspace:
    // they match nothing, so they cannot influence a render either way.
    ids.reserve(m_canvasProfileIds.size());
    for (const auto& id : m_canvasProfileIds) {
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

bool ProjectItem::detectInputCompositionChange() const
{
    // No outputs → nothing to invalidate; Pending inputs already force a full run.
    if (m_output_images.empty())
        return false;

    // No baseline (a project rendered before this axis existed) → don't guess here; load()
    // backfills the baseline from output provenance, so a genuine change is caught on the next pass.
    if (inputOrderAtRender.empty())
        return false;

    return orderedInputUids() != inputOrderAtRender;
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

std::vector<ProcessingError> ProjectItem::applyProcessingResults(
    const std::vector<ProcessingSliceRecord>& records,
    const std::vector<AppliedCanvasProfile>&  appliedProfiles,
    const std::vector<std::string>&           skippedInputPaths,
    const std::vector<CanvasProfile>&         workspaceProfiles,
    const std::string&                        outputDirectory,
    const std::string&                        timestamp)
{
    std::vector<ProcessingError> postRenderErrors; // returned: inputs that could not be hashed after render
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

    // Paths the run did not include (no matching/linked canvas profile, or a load error).
    const std::unordered_set<std::string> skipped(
        skippedInputPaths.begin(), skippedInputPaths.end());

    // Update each InputFile: hash, status, timestamp, contributesTo, canvas baseline.
    for (auto& inf : m_input_images) {
        // A skipped page was not rendered, so it must not claim to be Processed (that is the
        // "skipped pages silently go green" bug). Mark it Skipped and leave its hash/timestamp
        // untouched — nothing was produced for it, and it contributes to no output.
        if (skipped.count(inf.filePath)) {
            inf.status = FileStatus::Skipped;
            inf.contributesTo.clear();
            inf.canvasProfileId.clear();
            inf.canvasFingerprint.clear();
            continue;
        }

        const std::string h =
            Infrastructure::FileMetaData::computeFileSha256(inf.filePath);
        if (!h.empty()) {
            inf.sha256        = h;
            inf.status        = FileStatus::Processed;
            inf.lastProcessed = timestamp;
        } else {
            // The render succeeded and produced output from this input, but its content could not be
            // hashed afterwards (locked / permission / offline). Leaving it Pending (the old
            // `if (!h.empty())` skip) made sanitize() re-mark it Pending forever → the next render
            // redid everything and overwrote the output, silently, without end. Mark it Error instead:
            // it *was* rendered (record lastProcessed) but has no verification baseline (empty sha256),
            // and Error is sticky + non-forcing in sanitize(), so the loop is broken. Surface it so the
            // caller can tell the user which file to fix.
            inf.status        = FileStatus::Error;
            inf.lastProcessed = timestamp;
            postRenderErrors.push_back(ProcessingError{
                ProcessingErrorCode::InputHashFailed, ProcessingErrorCategory::Io,
                "Input could not be read to verify after render (locked / permission / offline).",
                inf.filePath, {}});
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
        // Positional but stable and unique within this wholesale rebuild — output_00N always maps to
        // out-N. (Unlike inputs, the output list is regenerated in full each render, so there is no
        // cross-scan collision to guard against.)
        outf.uid       = "out-" + std::to_string(i);
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

    // Baseline for detectInputCompositionChange(): the input sequence (uids, in strip order) this
    // render used. A later reorder / add / remove changes this and thus stales the outputs.
    inputOrderAtRender = orderedInputUids();

    rebuildLookupTables();

    return postRenderErrors;
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
            // uid is left empty here and minted uniquely by ensureUniqueFileUids() below — deriving it
            // from the list position ("file-" + index) handed the same id to different files across
            // re-scans.
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

    ensureUniqueFileUids();  // mint uids for the brand-new files (left empty above)
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
    if (std::find(m_canvasProfileIds.begin(), m_canvasProfileIds.end(), profileId)
            != m_canvasProfileIds.end())
        return true;

    // Locate the profile in the workspace palette.
    const CanvasProfile* adding = nullptr;
    for (const auto& p : workspaceProfiles)
        if (p.id == profileId) { adding = &p; break; }
    if (!adding) return false; // unknown ID

    // Conflict guard (SPECIFICATION.md §7.5.2):
    // Reject if any already-linked profile shares the same canvas dimensions.
    for (const auto& existingId : m_canvasProfileIds) {
        for (const auto& p : workspaceProfiles) {
            if (p.id == existingId &&
                p.canvasSize.width  == adding->canvasSize.width &&
                p.canvasSize.height == adding->canvasSize.height)
                return false;
        }
    }

    m_canvasProfileIds.push_back(profileId);
    return true;
}

// ---------------------------------------------------------------------------
// ensureUniqueFileUids
// ---------------------------------------------------------------------------

void ProjectItem::ensureUniqueFileUids()
{
    // Inputs and outputs are separate namespaces ("file-*" / "out-*"), so they are deduplicated
    // independently. Only an empty or already-seen uid is (re)minted, so a set that is already unique
    // is untouched and existing ids stay stable across loads.
    const auto repair = [](auto& files, std::string_view prefix) {
        std::vector<std::string> taken;
        taken.reserve(files.size());
        for (auto& f : files) {
            if (f.uid.empty() ||
                std::find(taken.begin(), taken.end(), f.uid) != taken.end())
                f.uid = Infrastructure::makeUniqueId(prefix, taken);
            taken.push_back(f.uid);
        }
    };

    repair(m_input_images,  "file");
    repair(m_output_images, "out");
}

} // namespace Platemaker::Models
