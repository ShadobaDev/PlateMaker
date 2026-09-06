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
    , processingSignature(std::move(other.processingSignature))
    , colourCorrection(std::move(other.colourCorrection))
    , m_canvasProfileIds(std::move(other.m_canvasProfileIds))
    , m_outputProfileId(std::move(other.m_outputProfileId))
    , m_inputImages(std::move(other.m_inputImages))
    , m_outputImages(std::move(other.m_outputImages))
    , m_stripOverlays(std::move(other.m_stripOverlays))
    , m_outputDirectory(std::move(other.m_outputDirectory))
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
        processingSignature   = std::move(other.processingSignature);
        colourCorrection      = std::move(other.colourCorrection);
        m_canvasProfileIds    = std::move(other.m_canvasProfileIds);
        m_outputProfileId     = std::move(other.m_outputProfileId);
        m_inputImages        = std::move(other.m_inputImages);
        m_outputImages       = std::move(other.m_outputImages);
        m_stripOverlays       = std::move(other.m_stripOverlays);
        m_outputDirectory    = std::move(other.m_outputDirectory);
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
    return m_inputImages;
}

const std::vector<InputFile>& ProjectItem::getInputImages() const noexcept
{
    return m_inputImages;
}

std::vector<InputFile> ProjectItem::inputsInOrder() const
{
    std::vector<InputFile> ordered = m_inputImages;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const InputFile& a, const InputFile& b) { return a.order < b.order; });
    return ordered;
}

std::vector<std::string> ProjectItem::orderedInputUids() const
{
    std::vector<const InputFile*> ptrs;
    ptrs.reserve(m_inputImages.size());
    for (const auto& f : m_inputImages)
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
    return m_outputImages;
}

const std::vector<OutputFile>& ProjectItem::getOutputImages() const noexcept
{
    return m_outputImages;
}

std::string& ProjectItem::getOutputDirectory() noexcept
{
    return m_outputDirectory;
}

const std::string& ProjectItem::getOutputDirectory() const noexcept
{
    return m_outputDirectory;
}

// ---------------------------------------------------------------------------
// Strip overlays — inventory parallel to input files
// ---------------------------------------------------------------------------

std::vector<StripOverlay>& ProjectItem::getStripOverlays() noexcept
{
    return m_stripOverlays;
}

const std::vector<StripOverlay>& ProjectItem::getStripOverlays() const noexcept
{
    return m_stripOverlays;
}

std::string ProjectItem::addOverlay(const std::string& assetPath, int x, int y, BlendMode blend,
                                    const std::string& anchorInputUid)
{
    std::vector<std::string> taken;
    taken.reserve(m_stripOverlays.size());
    for (const auto& o : m_stripOverlays)
        taken.push_back(o.uid);

    StripOverlay ov;
    ov.uid   = Infrastructure::makeUniqueId("ovl", taken);
    ov.x     = x;
    ov.y     = y;
    ov.blend = blend;
    ov.enabled = true;
    ov.anchorInputUid = anchorInputUid;

    // Hash the content (dedup + staleness). A hash failure is non-fatal: the overlay is still
    // registered with an empty sha (no dedup, and a later silent content change won't be caught until
    // it is re-added) — the same tolerance applyProcessingResults() gives an unhashable input.
    try {
        ov.sha256 = Infrastructure::FileMetaData::computeFileSha256(assetPath);
    } catch (const std::exception&) {
        ov.sha256.clear();
    }

    // Dedup by content: if an existing overlay references an asset with the same hash, reuse that stored
    // path so identical content is kept once (the consumer can drop its duplicate file).
    ov.assetPath = assetPath;
    if (!ov.sha256.empty()) {
        for (const auto& e : m_stripOverlays) {
            if (e.sha256 == ov.sha256 && !e.assetPath.empty()) {
                ov.assetPath = e.assetPath;
                break;
            }
        }
    }

    m_stripOverlays.push_back(std::move(ov));
    return m_stripOverlays.back().uid;
}

bool ProjectItem::removeOverlay(const std::string& overlayUid)
{
    const auto it = std::find_if(m_stripOverlays.begin(), m_stripOverlays.end(),
                                 [&](const StripOverlay& o) { return o.uid == overlayUid; });
    if (it == m_stripOverlays.end())
        return false;
    m_stripOverlays.erase(it);
    return true;
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
// dirtyOutputNames / inputsAllProcessed
// ---------------------------------------------------------------------------

std::vector<std::string> ProjectItem::dirtyOutputNames() const
{
    std::vector<std::string> names;
    for (const auto& out : m_outputImages)
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
    for (const auto& inf : m_inputImages)
        if (inf.status != FileStatus::Processed &&
            inf.status != FileStatus::Skipped   &&
            inf.status != FileStatus::Error)
            return false;
    return !m_inputImages.empty();
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
    if (m_outputImages.empty())
        return change;

    // The project's effective/assigned profile list, in the order the matcher applies it. This
    // mirrors CanvasProfileMatcher's subA exactly (see effectiveCanvasProfileIds() /
    // canvas_profile_matcher.cpp): an empty per-project list means "accept all workspace profiles"
    // in workspace order; an explicit list is used in project-priority order. Workspace-only
    // profiles are deliberately NOT here — the matcher never *applies* them (a same-size unlinked
    // profile returns FoundInWorkspaceOnly, i.e. the page renders without a profile), so a page that
    // only matches an unlinked profile is treated as "no profile" and adding that profile never
    // desyncs this project. Resolving ids to profiles here cannot drop any: effectiveCanvasProfileIds
    // already omits ids absent from the workspace.
    const std::vector<std::string> effectiveIds = effectiveCanvasProfileIds(workspaceProfiles);
    std::vector<const CanvasProfile*> effective;
    effective.reserve(effectiveIds.size());
    for (const auto& id : effectiveIds) {
        const auto it = std::find_if(workspaceProfiles.begin(), workspaceProfiles.end(),
            [&id](const CanvasProfile& cp) { return cp.id == id; });
        if (it != workspaceProfiles.end())
            effective.push_back(&*it);
    }

    // Per-page re-match. For each input, decide whether the profile it would render with *now*
    // differs from the one recorded at the last render.
    bool anyUnknownDims = false;
    for (const auto& inf : m_inputImages) {
        if (inf.width > 0 && inf.height > 0) {
            // Dimensions known → resolve precisely: the first effective profile of this W×H, identical
            // to what CanvasProfileMatcher::resolveForSize() returns as Matched (the conflict guard makes it
            // final for a linked list; for accept-all the first in effective order wins, as the matcher
            // does). A page is stale only if its applied profile changed — a different id (including
            // "" ⇄ id: a newly-added profile now matches a page that had none, or a removed/reordered
            // one), or the same id whose render-relevant fields were edited (fingerprint differs). This
            // is what makes a profile that matches *no* page stay silent instead of desyncing the lot.
            const CanvasProfile* now = nullptr;
            for (const auto* cp : effective) {
                if (canvasSizeMatches(*cp, inf.width, inf.height)) { now = cp; break; }
            }

            const std::string nowId = now ? now->id : std::string{};
            const std::string nowFp = now ? canvasRenderFingerprint(*now) : std::string{};

            if (nowId != inf.canvasProfileId || nowFp != inf.canvasFingerprint)
                change.changedInputs.push_back(inf.filePath);
        } else {
            // Dimensions unknown (legacy page rendered before they were tracked) → cannot re-match.
            // Fall back to the per-input fingerprint check, which still catches an in-place edit or a
            // deletion of the profile this page recorded, and flag for the coarse list comparison below,
            // which is the only thing that can catch a newly-added profile now matching a page that
            // recorded none (there is no baseline to compare that page against).
            anyUnknownDims = true;
            if (!inf.canvasProfileId.empty()) {
                const auto it = std::find_if(workspaceProfiles.begin(), workspaceProfiles.end(),
                    [&inf](const CanvasProfile& cp) { return cp.id == inf.canvasProfileId; });
                if (it == workspaceProfiles.end() ||
                    canvasRenderFingerprint(*it) != inf.canvasFingerprint)
                    change.changedInputs.push_back(inf.filePath);
            }
        }
    }

    // Coarse fallback — engaged only when some page has no stored dimensions (a legacy record the
    // precise pass could not re-match). When every page's dimensions are known the precise pass is
    // complete and authoritative, so listChanged stays false and adding a profile that matches nothing
    // produces no warning. A legacy workspace thus takes one full re-render to record dimensions and
    // become precise — the honest outcome, since those outputs may genuinely be stale.
    change.listChanged = anyUnknownDims && (effectiveIds != canvasProfileIdsAtRender);

    return change;
}

bool ProjectItem::detectInputCompositionChange() const
{
    // No outputs → nothing to invalidate; Pending inputs already force a full run.
    if (m_outputImages.empty())
        return false;

    // No baseline (a project rendered before this axis existed) → don't guess here; load()
    // backfills the baseline from output provenance, so a genuine change is caught on the next pass.
    if (inputOrderAtRender.empty())
        return false;

    return orderedInputUids() != inputOrderAtRender;
}

// ---------------------------------------------------------------------------
// detectStaleness
// ---------------------------------------------------------------------------

StalenessReport ProjectItem::detectStaleness(
    const std::vector<CanvasProfile>& workspaceProfiles,
    const OutputProfile&              outputProfile) const
{
    StalenessReport report;

    report.hasOutputs = !m_outputImages.empty();

    // Not recomputed: this is what the last sanitize() concluded after hashing every file. Doing it
    // again here would make a status refresh cost a full re-hash of the project.
    report.contentDirty = !m_isUpToDate;

    // The output profile can change every byte while leaving the geometry — and therefore every
    // sourceMap — identical, so only a stored signature catches it. Guarded on non-empty: a project
    // rendered before signatures existed has nothing to compare and must not be flagged forever.
    const std::string currentSignature = outputProfileSignature(outputProfile);
    report.outputProfileChanged =
        !outputSignature.empty() && outputSignature != currentSignature;

    // A format switch is visible even without that baseline, because the recorded file names carry
    // the old extension. It is also the one axis that orphans *every* existing output: the new
    // settings cannot produce any of the old names.
    if (report.hasOutputs) {
        const std::string  wantedExtension = outputFormatExtension(outputProfile.outputFormat);
        const std::string& firstName       = m_outputImages.front().fileName;
        const auto         dot             = firstName.find_last_of('.');
        const std::string  storedExtension =
            (dot == std::string::npos) ? std::string{} : firstName.substr(dot);
        report.outputFormatChanged = !storedExtension.empty() && storedExtension != wantedExtension;
    }

    report.canvas                  = detectCanvasConfigChange(workspaceProfiles);
    report.inputCompositionChanged = detectInputCompositionChange();

    // Colour correction and overlays change output bytes while touching no input or output file.
    // No empty-guard here, unlike the output signature: "no processing" has an empty signature, so a
    // project that never used a step matches and stays quiet — but turning a step back *off* must
    // still differ from the non-empty signature stored when it was on, and re-render.
    const std::string currentProcessing =
        processingConfigSignature(colourCorrection, m_stripOverlays);
    report.processingStepsChanged = processingSignature != currentProcessing;

    return report;
}

// ---------------------------------------------------------------------------
// rebuildLookupTables
// ---------------------------------------------------------------------------

void ProjectItem::rebuildLookupTables()
{
    m_inputToOutputLookup.clear();
    m_sha256Index.clear();

    for (const auto& inf : m_inputImages) {
        // Map filePath → [output file names it contributed to].
        m_inputToOutputLookup[inf.filePath] = inf.contributesTo;

        // Map sha256 → filePath (for rename detection).
        if (!inf.sha256.empty())
            m_sha256Index[inf.sha256] = inf.filePath;
    }
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

    repair(m_inputImages,  "file");
    repair(m_outputImages, "out");
}

} // namespace Platemaker::Models
