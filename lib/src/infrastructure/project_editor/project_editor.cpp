/**
 * \file lib/src/infrastructure/project_editor/project_editor.cpp
 * \brief ProjectEditor implementation — input ordering for a single project.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-29
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <platemaker/infrastructure/project_editor/project_editor.hpp>

#include "infrastructure/model_json/model_json.hpp"   // ProjectItem JSON codec

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <filesystem>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>

namespace Platemaker::Infrastructure {

bool ProjectEditor::setInputOrder(const std::vector<std::string>& orderedUids)
{
    auto& inputs = m_project.getInputImages();

    // Must be a permutation of the current input uids: same count, distinct, and each one exists.
    if (orderedUids.size() != inputs.size())
        return false;

    std::unordered_map<std::string, Models::InputFile*> byUid;
    byUid.reserve(inputs.size());
    for (auto& f : inputs)
        byUid.emplace(f.uid, &f);

    const std::unordered_set<std::string> distinct(orderedUids.begin(), orderedUids.end());
    if (distinct.size() != inputs.size())
        return false; // a duplicate (and therefore a missing) uid

    for (const auto& uid : orderedUids)
        if (byUid.find(uid) == byUid.end())
            return false; // an unknown uid

    // Valid permutation → assign order = position. The vector itself is left untouched.
    for (int i = 0; i < static_cast<int>(orderedUids.size()); ++i)
        byUid[orderedUids[static_cast<std::size_t>(i)]]->order = i;

    return true;
}

bool ProjectEditor::moveInput(const std::string& uid, int delta)
{
    const auto& inputs = m_project.getInputImages();
    if (inputs.size() < 2)
        return false;

    // Current strip order (by the order field), as uids.
    std::vector<const Models::InputFile*> ptrs;
    ptrs.reserve(inputs.size());
    for (const auto& f : inputs)
        ptrs.push_back(&f);
    std::stable_sort(ptrs.begin(), ptrs.end(),
                     [](const Models::InputFile* a, const Models::InputFile* b) {
                         return a->order < b->order;
                     });

    std::vector<std::string> ordered;
    ordered.reserve(ptrs.size());
    for (const auto* p : ptrs)
        ordered.push_back(p->uid);

    int idx = -1;
    for (int i = 0; i < static_cast<int>(ordered.size()); ++i)
        if (ordered[static_cast<std::size_t>(i)] == uid) { idx = i; break; }

    const int target = idx + delta;
    if (idx < 0 || target < 0 || target >= static_cast<int>(ordered.size()))
        return false; // unknown uid or already at the edge

    std::swap(ordered[static_cast<std::size_t>(idx)], ordered[static_cast<std::size_t>(target)]);
    return setInputOrder(ordered);
}

bool ProjectEditor::replaceInputFile(const std::string& uid, const std::string& newPath)
{
    if (newPath.empty())
        return false;

    Models::InputFile* target = nullptr;
    for (auto& f : m_project.m_inputImages) {
        if (f.uid == uid)
            target = &f;
        else if (f.filePath == newPath)
            return false;   // another page's file — a rescan matches by path and would fold the two
    }
    if (!target || target->filePath == newPath)
        return false;

    // Identity stays: the uid is what overlays and grade exclusions point at, the order is where the
    // page sits. Nothing else about the entry is kept, because all of it described the old file.
    target->filePath = newPath;
    target->sha256.clear();
    target->status = Models::FileStatus::Pending;
    target->lastProcessed.clear();
    target->thumbnailPath.clear();
    target->canvasProfileId.clear();
    target->canvasFingerprint.clear();
    target->width  = 0;
    target->height = 0;

    // The slices are not marked: a Pending page in a rendered project is what detectFileStaleness()
    // reports as putting every slice out of sync.
    m_project.m_isUpToDate = false;

    m_project.rebuildLookupTables();   // keyed by path, and the path just changed
    return true;
}

std::string ProjectEditor::snapshot() const
{
    // to_json(ProjectItem) reads the profile links via the public accessors, so a plain conversion
    // captures the full project. Compact dump keeps the in-memory undo snapshot small.
    const nlohmann::json j = m_project;
    return j.dump();
}

void ProjectEditor::restore(const std::string& snapshot)
{
    const nlohmann::json j = nlohmann::json::parse(snapshot);

    // name is workspace-owned (renamed via WorkspaceEditor on the workspace timeline); a project-scope
    // restore must keep the current name rather than resurrect the one stored in the snapshot.
    std::string keepName = m_project.name;

    // from_json fills the public fields + inputs/outputs but deliberately skips the private link
    // fields; set those from the same JSON through the friend path (a previously valid state).
    Models::ProjectItem restored = j.get<Models::ProjectItem>();
    restored.name              = std::move(keepName);
    restored.m_canvasProfileIds = j.value("canvasProfileIds", std::vector<std::string>{});
    restored.m_outputProfileId  = j.value("outputProfileId",  std::string{});
    restored.rebuildLookupTables();

    m_project = std::move(restored);
}

// ---------------------------------------------------------------------------
// sanitize
// ---------------------------------------------------------------------------

bool ProjectEditor::sanitize()
{
    std::vector<DiskCheck> checks;
    for (const auto& request : diskCheckRequests())
        checks.push_back(checkOnDisk(request));
    return applyDiskChecks(checks);
}

std::vector<DiskCheckRequest> ProjectEditor::diskCheckRequests() const
{
    std::vector<DiskCheckRequest> requests;
    requests.reserve(m_project.m_inputImages.size() + m_project.m_outputImages.size());
    for (const auto& file : m_project.m_inputImages)
        requests.push_back({file.filePath,
                            !file.sha256.empty() || file.status == Models::FileStatus::Error,
                            file.sha256});
    for (const auto& out : m_project.m_outputImages)
        requests.push_back({m_project.m_outputDirectory + "/" + out.fileName, !out.sha256.empty(), out.sha256});
    return requests;
}

DiskCheck ProjectEditor::checkOnDisk(const DiskCheckRequest& request)
{
    DiskCheck check;
    check.path           = request.path;
    check.recordedSha256 = request.recordedSha256;
    // utf8ToPath() rather than the bare string: on MSVC a narrow path is read in the ANSI
    // code page, so a non-ASCII path would report as Missing even though the file is there.
    check.exists = std::filesystem::exists(Infrastructure::utf8ToPath(request.path));
    if (check.exists && request.hash)
        check.sha256 = Infrastructure::FileMetaData::computeFileSha256(request.path);
    return check;
}

bool ProjectEditor::applyDiskChecks(const std::vector<DiskCheck>& checks)
{
    std::unordered_map<std::string, const DiskCheck*> byPath;
    byPath.reserve(checks.size());
    for (const auto& check : checks)
        byPath[check.path] = &check;
    // A result measured against a record that has changed since (a render wrote the file and recorded its
    // new hash) describes a file that is gone; it counts as no result.
    const auto find = [&byPath](const std::string& path, const std::string& recorded) -> const DiskCheck* {
        const auto it = byPath.find(path);
        return (it == byPath.end() || it->second->recordedSha256 != recorded) ? nullptr : it->second;
    };

    for (auto& file : m_project.m_inputImages) {
        const DiskCheck* check = find(file.filePath, file.sha256);
        if (!check)
            continue;   // not looked at this time: what it was stays
        if (!check->exists) {
            file.status = Models::FileStatus::Missing;
            continue;
        }

        // An input the last render produced output for but could not hash afterwards (locked /
        // permission / offline) stays Error until the file becomes readable again. A hash now means
        // access was restored → recover to Processed and adopt the current content as the
        // verification baseline; none means still unreachable → remain Error. Like Skipped, Error
        // has an empty sha256 and does NOT mark the project out of date — that is exactly what stops the
        // silent re-render loop (falling through to the empty-sha256 branch below would re-mark it
        // Pending and reprocess everything forever).
        if (file.status == Models::FileStatus::Error) {
            if (check->sha256.empty())
                continue; // still unreadable — stay Error, do not force a re-render
            file.sha256 = check->sha256;
            file.status = Models::FileStatus::Processed;
            continue;
        }

        // A page the last render skipped (its file missing or failed to load) stays Skipped as long
        // as the file is unchanged. The disk cannot re-derive why it was skipped, so it must NOT
        // silently "un-skip" the page to Processed/Pending — doing so lost the render's result on every
        // reopen, and made the project look permanently out of date (a never-rendered skipped page has
        // no hash → Pending → forces a re-render forever, even with every output Done). Skipped is
        // terminal, so it does NOT mark the project out of date.
        if (file.status == Models::FileStatus::Skipped) {
            if (file.sha256.empty() || check->sha256 == file.sha256)
                continue; // no content baseline, or unchanged → remain Skipped
            // The file changed since it was skipped: fall through so it becomes Modified and the next
            // render re-includes or re-skips it.
        }

        if (file.sha256.empty())
            file.status = Models::FileStatus::Pending;   // never processed — the pipeline has to run
        else
            file.status = check->sha256 == file.sha256 ? Models::FileStatus::Processed
                                                       : Models::FileStatus::Modified;
    }

    // Validate output slices against disk: a deleted or externally-edited
    // output makes the project out-of-date even when every input is unchanged.
    for (auto& out : m_project.m_outputImages) {
        const DiskCheck* check = find(m_project.m_outputDirectory + "/" + out.fileName, out.sha256);
        if (!check)
            continue;
        if (!check->exists)
            out.status = Models::FileStatus::Missing;
        else if (out.sha256.empty())
            out.status = Models::FileStatus::Done;   // no stored hash to compare (legacy record) — trust existence
        else
            out.status = check->sha256 == out.sha256 ? Models::FileStatus::Done
                                                     : Models::FileStatus::Modified;
    }

    // What the configuration has put out of sync is not recorded here: a profile edit, a reorder or a
    // grade leaves every file byte-identical, and a status written now would be wrong after the next
    // edit or its undo. ProjectItem::detectFileStaleness() answers it from the model when asked.
    //
    // Up to date is read off the statuses, so a file this call had no result for counts as it stands.
    const auto settled = [](Models::FileStatus s) {
        return s != Models::FileStatus::Missing && s != Models::FileStatus::Pending
            && s != Models::FileStatus::Modified;
    };
    m_project.m_isUpToDate =
        std::all_of(m_project.m_inputImages.begin(), m_project.m_inputImages.end(),
                    [&](const Models::InputFile& f) { return settled(f.status); })
        && std::all_of(m_project.m_outputImages.begin(), m_project.m_outputImages.end(),
                       [&](const Models::OutputFile& o) { return settled(o.status); });
    return m_project.m_isUpToDate;
}

// ---------------------------------------------------------------------------
// applyProcessingResults
// ---------------------------------------------------------------------------

std::vector<Models::ProcessingError> ProjectEditor::applyProcessingResults(
    const std::vector<Models::ProcessingSliceRecord>& records,
    const std::vector<Models::AppliedCanvasProfile>&  appliedProfiles,
    const std::vector<std::string>&           skippedInputPaths,
    const std::vector<Models::CanvasProfile>&         workspaceProfiles,
    const std::string&                        outputDirectory,
    const std::string&                        timestamp)
{
    std::vector<Models::ProcessingError> postRenderErrors; // returned: inputs that could not be hashed after render
    // Build contributesTo map: filePath → [output file names].
    std::unordered_map<std::string, std::vector<std::string>> contributes;
    for (const auto& rec : records)
        for (const auto& seg : rec.sourceMap)
            contributes[seg.sourceFilePath].push_back(rec.fileName);

    // filePath → canvas profile the run applied to it.
    std::unordered_map<std::string, const Models::AppliedCanvasProfile*> applied;
    applied.reserve(appliedProfiles.size());
    for (const auto& ap : appliedProfiles)
        applied[ap.sourceFilePath] = &ap;

    // Paths the run did not include (no matching/linked canvas profile, or a load error).
    const std::unordered_set<std::string> skipped(
        skippedInputPaths.begin(), skippedInputPaths.end());

    // Update each Models::InputFile: hash, status, timestamp, contributesTo, canvas baseline.
    for (auto& inf : m_project.m_inputImages) {
        // A skipped page was not rendered, so it must not claim to be Processed (that is the
        // "skipped pages silently go green" bug). Mark it Skipped and leave its hash/timestamp
        // untouched — nothing was produced for it, and it contributes to no output.
        if (skipped.count(inf.filePath)) {
            inf.status = Models::FileStatus::Skipped;
            inf.contributesTo.clear();
            inf.canvasProfileId.clear();
            inf.canvasFingerprint.clear();
            continue;
        }

        const std::string h =
            Infrastructure::FileMetaData::computeFileSha256(inf.filePath);
        if (!h.empty()) {
            inf.sha256        = h;
            inf.status        = Models::FileStatus::Processed;
            inf.lastProcessed = timestamp;
        } else {
            // The render succeeded and produced output from this input, but its content could not be
            // hashed afterwards (locked / permission / offline). Leaving it Pending (the old
            // `if (!h.empty())` skip) made sanitize() re-mark it Pending forever → the next render
            // redid everything and overwrote the output, silently, without end. Mark it Error instead:
            // it *was* rendered (record lastProcessed) but has no verification baseline (empty sha256),
            // and Error is sticky + non-forcing in sanitize(), so the loop is broken. Surface it so the
            // caller can tell the user which file to fix.
            inf.status        = Models::FileStatus::Error;
            inf.lastProcessed = timestamp;
            postRenderErrors.push_back(Models::ProcessingError{
                Models::ProcessingErrorCode::InputHashFailed, Models::ProcessingErrorCategory::Io,
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
            // Record the display W×H the run resolved against, so detectCanvasConfigChange() can
            // re-match this page offline instead of blanket-invalidating on any profile-list change.
            // Only overwrite from a real reading (>0): a page the run could not measure keeps whatever
            // dimensions a previous successful render established rather than dropping to "unknown".
            if (ap->second->width  > 0) inf.width  = ap->second->width;
            if (ap->second->height > 0) inf.height = ap->second->height;
        } else {
            inf.canvasProfileId.clear();
            inf.canvasFingerprint.clear();
            // Leave width/height as-is: dimensions are a property of the file, not the render config,
            // and a page the run never reached (Missing) is better served by its last-known size.
        }
    }

    // Rebuild Models::OutputFile list from records.
    m_project.m_outputImages.clear();
    m_project.m_outputImages.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        Models::OutputFile outf;
        // Positional but stable and unique within this wholesale rebuild — output_00N always maps to
        // out-N. (Unlike inputs, the output list is regenerated in full each render, so there is no
        // cross-scan collision to guard against.)
        outf.uid       = "out-" + std::to_string(i);
        outf.fileName  = records[i].fileName;
        outf.sha256    = records[i].outputSha256;
        outf.sourceMap = records[i].sourceMap;
        outf.status    = Models::FileStatus::Done;
        m_project.m_outputImages.push_back(std::move(outf));
    }

    m_project.m_outputDirectory = outputDirectory;
    m_project.m_isUpToDate       = true;

    // Baseline for detectCanvasConfigChange(): the profile list this render used.
    // Captured here rather than by the caller so it can never drift out of sync with
    // the per-input fingerprints written above.
    m_project.canvasProfileIdsAtRender = m_project.effectiveCanvasProfileIds(workspaceProfiles);

    // Baseline for detectInputCompositionChange(): the input sequence (uids, in strip order) this
    // render used. A later reorder / add / remove changes this and thus stales the outputs.
    m_project.inputOrderAtRender = m_project.orderedInputUids();

    m_project.rebuildLookupTables();

    return postRenderErrors;
}

// ---------------------------------------------------------------------------
// applyPartialResults
// ---------------------------------------------------------------------------

void ProjectEditor::applyPartialResults(
    const std::vector<Models::ProcessingSliceRecord>& records)
{
    // Partial re-render: only the dirty output slices were regenerated. Inputs
    // are unchanged, so their hashes / provenance / contributesTo stay as-is —
    // we just refresh the hash and clear the dirty status of the rewritten
    // outputs. (Unlike applyProcessingResults, which rebuilds everything.)
    for (const auto& rec : records) {
        for (auto& out : m_project.m_outputImages) {
            if (out.fileName == rec.fileName) {
                out.sha256    = rec.outputSha256;
                out.sourceMap = rec.sourceMap;
                out.status    = Models::FileStatus::Done;
                break;
            }
        }
    }

    // Up-to-date only if no output remains dirty (and inputs were already clean).
    m_project.m_isUpToDate = true;
    for (const auto& out : m_project.m_outputImages) {
        if (out.status != Models::FileStatus::Done) {
            m_project.m_isUpToDate = false;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// mergeFileScan
// ---------------------------------------------------------------------------

Models::ScanMergeResult ProjectEditor::mergeFileScan(
    const std::vector<std::string>& newFilePaths)
{
    Models::ScanMergeResult result;

    // -------------------------------------------------------------------
    // Build lookup maps from the existing input list.
    // -------------------------------------------------------------------

    // path → index in m_project.m_inputImages (for path-based matching).
    std::unordered_map<std::string, std::size_t> pathToOldIdx;
    pathToOldIdx.reserve(m_project.m_inputImages.size());
    for (std::size_t i = 0; i < m_project.m_inputImages.size(); ++i)
        pathToOldIdx[m_project.m_inputImages[i].filePath] = i;

    // sha256 → index in m_project.m_inputImages (for rename detection).
    // Only populate for files that have been processed (non-empty sha256).
    std::unordered_map<std::string, std::size_t> sha256ToOldIdx;
    sha256ToOldIdx.reserve(m_project.m_inputImages.size());
    for (std::size_t i = 0; i < m_project.m_inputImages.size(); ++i) {
        const auto& inf = m_project.m_inputImages[i];
        if (!inf.sha256.empty())
            sha256ToOldIdx[inf.sha256] = i;
    }

    // Set of new paths for fast "is the old file still here?" check.
    std::unordered_set<std::string> newPathSet(
        newFilePaths.begin(), newFilePaths.end());

    // -------------------------------------------------------------------
    // Build the new ordered list.
    // -------------------------------------------------------------------
    std::vector<Models::InputFile> newList;
    newList.reserve(newFilePaths.size());

    // Track which old indices were consumed (to find removals later).
    std::unordered_set<std::size_t> consumedOldIndices;

    bool structuralChange = false;

    for (int newOrder = 0; newOrder < static_cast<int>(newFilePaths.size()); ++newOrder) {
        const std::string& newPath = newFilePaths[static_cast<std::size_t>(newOrder)];

        // --- Case 1: same path exists in old list ---
        const auto pathIt = pathToOldIdx.find(newPath);
        if (pathIt != pathToOldIdx.end()) {
            Models::InputFile inf = m_project.m_inputImages[pathIt->second];
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
            Models::InputFile inf = m_project.m_inputImages[oldIdx];
            result.renamed.push_back(newPath);
            inf.filePath = newPath; // update path, keep everything else
            // If the strip position changed, that's a structural change.
            if (inf.order != newOrder) structuralChange = true;
            inf.order = newOrder;
            consumedOldIndices.insert(oldIdx);
            newList.push_back(std::move(inf));
        } else {
            // --- Case 3: brand-new file ---
            Models::InputFile inf;
            // uid is left empty here and minted uniquely by ensureUniqueFileUids() below — deriving it
            // from the list position ("file-" + index) handed the same id to different files across
            // re-scans.
            inf.filePath = newPath;
            inf.order    = newOrder;
            inf.status   = Models::FileStatus::Pending;
            newList.push_back(std::move(inf));
            result.added.push_back(newPath);
            structuralChange = true;
        }
    }

    // -------------------------------------------------------------------
    // Detect removed files (old indices not consumed and path not in newPaths).
    // -------------------------------------------------------------------
    for (std::size_t i = 0; i < m_project.m_inputImages.size(); ++i) {
        if (consumedOldIndices.count(i) == 0) {
            result.removed.push_back(m_project.m_inputImages[i].filePath);
            structuralChange = true;
        }
    }

    // -------------------------------------------------------------------
    // Apply structural change consequences.
    // -------------------------------------------------------------------
    // The slices are not marked: an added page is Pending and a removed one changes the composition,
    // and detectFileStaleness() reports either from the model.
    if (structuralChange)
        result.outputsInvalidated = true;

    // Replace the old input list with the new one.
    m_project.m_inputImages = std::move(newList);
    m_project.m_isUpToDate   = false; // always requires at least a sanitize() pass

    m_project.ensureUniqueFileUids();  // mint uids for the brand-new files (left empty above)
    m_project.rebuildLookupTables();
    return result;
}

} // namespace Platemaker::Infrastructure
