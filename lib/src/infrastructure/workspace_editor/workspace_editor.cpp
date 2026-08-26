/**
 * \file lib/src/infrastructure/workspace_editor/workspace_editor.cpp
 * \brief WorkspaceEditor implementation — the one place workspace invariants are enforced.
 *
 * The identifier-repair helpers here used to live in the anonymous namespace of
 * workspace_serializer.cpp, reachable only through load().  They now back both load()
 * (via installLoaded) and every in-session edit, so a workspace read from disk and one
 * edited in memory obey exactly the same rules.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include "infrastructure/model_json/model_json.hpp"   // Workspace-component JSON codec

#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/models/output_profile.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace Platemaker::Infrastructure {

// ---------------------------------------------------------------------------
// Identifier repair helpers
//
// Order matters where they run together (installLoaded): mint first, then deduplicate —
// two id-less profiles sharing a name used to derive the same legacy id, so minting after
// the dedup pass would leave that collision in place.
// ---------------------------------------------------------------------------

void WorkspaceEditor::relinkProfileId(Models::Workspace& ws,
                                      const std::string& oldId,
                                      const std::string& newId)
{
    // Covers all four places a profile id is stored on a project: the assigned canvas
    // profiles, the output profile, the canvas baseline recorded at render time, and the
    // per-input profile the page was rendered with.
    for (auto& pi : ws.projectItems) {
        for (auto& id : pi.m_canvasProfileIds)   // private; WorkspaceEditor is a friend of ProjectItem
            if (id == oldId) id = newId;

        for (auto& id : pi.canvasProfileIdsAtRender)
            if (id == oldId) id = newId;

        if (pi.m_outputProfileId == oldId)
            pi.m_outputProfileId = newId;

        for (auto& inf : pi.getInputImages())
            if (inf.canvasProfileId == oldId)
                inf.canvasProfileId = newId;
    }
}

void WorkspaceEditor::mintMissingProfileIds(Models::Workspace& ws)
{
    // Pre-id workspaces (and, until 0.2.1, any profile a GUI saved with an empty id) had their
    // id computed as "cp-<name>" / "op-<name>".  Since that old form is deterministic, we can
    // compute what a profile's id would have been and relink exactly those references, turning
    // the name-derived scheme into a one-off migration instead of a permanent fixture.
    for (auto& cp : ws.m_canvasProfiles) {
        if (!cp.id.empty()) continue;
        cp.id = makeUniqueCanvasProfileId(ws.m_canvasProfiles);
        relinkProfileId(ws, "cp-" + cp.name, cp.id);
    }

    for (auto& op : ws.m_outputProfiles) {
        if (!op.id.empty()) continue;
        op.id = makeUniqueOutputProfileId(ws.m_outputProfiles);
        relinkProfileId(ws, "op-" + op.name, op.id);
    }
}

void WorkspaceEditor::migrateOutputProfilePresets(Models::Workspace& ws)
{
    // Presets used to be written into outputProfiles; they are now baked into the build and
    // resolved from the catalogue at runtime, so a persisted preset must be removed.  Touch
    // ONLY profiles that were persisted as presets, identified by their id — never a user's own
    // profile, even one whose settings happen to equal a preset (a legitimate copy must survive).
    const auto presets = Models::outputProfilePresets();
    auto&      ops     = ws.m_outputProfiles;

    for (auto it = ops.begin(); it != ops.end();) {
        // (A) Carries a canonical preset id → it was persisted as a preset, not a user profile.
        if (const auto preset = Models::outputProfilePresetById(it->id)) {
            if (Models::outputProfileSignature(*it) == Models::outputProfileSignature(*preset)) {
                it = ops.erase(it); // redundant copy; references resolve via the catalogue
            } else {
                const std::string oldId = it->id; // diverged → strip the reserved id
                it->id = makeUniqueOutputProfileId(ops);
                relinkProfileId(ws, oldId, it->id);
                ++it;
            }
            continue;
        }

        // (B) Legacy "op-<presetName>" that still matches the preset → pre-adoption persisted preset.
        bool collapsed = false;
        for (const auto& preset : presets) {
            if (it->id == "op-" + preset.name &&
                Models::outputProfileSignature(*it) == Models::outputProfileSignature(preset)) {
                relinkProfileId(ws, it->id, preset.id);
                it = ops.erase(it);
                collapsed = true;
                break;
            }
        }
        if (!collapsed) ++it; // any other profile (incl. a user copy of a preset) is left alone
    }
}

template <typename Profiles, typename MakeId>
void WorkspaceEditor::deduplicateIds(
    Profiles&                                              profiles,
    const MakeId&                                          makeFreshId,
    std::vector<WorkspaceRepairReport::ReassignedProfile>& out)
{
    // The first profile holding an id keeps it — every existing project reference still resolves,
    // and to the same profile it resolved to before.  Later duplicates get a fresh id and become
    // unassigned, putting them back in the "assign a profile" list they had silently dropped out
    // of.  Deliberately does NOT touch project references to the duplicated id: which of the two a
    // project really rendered with is settled precisely by ProjectItem::sanitize().
    std::vector<std::string> seen;
    seen.reserve(profiles.size());

    for (auto& p : profiles) {
        if (std::find(seen.begin(), seen.end(), p.id) == seen.end()) {
            seen.push_back(p.id);
            continue;
        }

        const std::string oldId = p.id;
        p.id = makeFreshId(profiles);
        seen.push_back(p.id);
        out.push_back({p.name, oldId, p.id});
    }
}

// ---------------------------------------------------------------------------
// Canvas profile palette
// ---------------------------------------------------------------------------

std::string WorkspaceEditor::addCanvasProfile(Models::CanvasProfile p)
{
    p.id = makeUniqueCanvasProfileId(m_ws.m_canvasProfiles);
    m_ws.m_canvasProfiles.push_back(std::move(p));
    return m_ws.m_canvasProfiles.back().id;
}

bool WorkspaceEditor::removeCanvasProfile(const std::string& id)
{
    const auto before = m_ws.m_canvasProfiles.size();
    m_ws.m_canvasProfiles.erase(
        std::remove_if(m_ws.m_canvasProfiles.begin(), m_ws.m_canvasProfiles.end(),
                       [&id](const Models::CanvasProfile& cp) { return cp.id == id; }),
        m_ws.m_canvasProfiles.end());
    return m_ws.m_canvasProfiles.size() != before;
}

WorkspaceRepairReport WorkspaceEditor::replaceCanvasProfiles(
    std::vector<Models::CanvasProfile> incoming)
{
    // Carry templateInfo from the current profile of the same id — the workspace is the source
    // of truth (a manage dialog drops the field on its round trip, and may carry a stale copy).
    for (auto& p : incoming) {
        for (const auto& cur : m_ws.m_canvasProfiles) {
            if (!cur.id.empty() && cur.id == p.id) {
                p.templateInfo = cur.templateInfo;
                break;
            }
        }
    }

    m_ws.m_canvasProfiles = std::move(incoming);

    WorkspaceRepairReport report;
    mintMissingProfileIds(m_ws);
    deduplicateIds(
        m_ws.m_canvasProfiles,
        [](const auto& existing) { return makeUniqueCanvasProfileId(existing); },
        report.canvasProfiles);
    return report;
}

bool WorkspaceEditor::setCanvasProfileTemplateInfo(const std::string& id,
                                                   Models::CanvasTemplateInfo info)
{
    for (auto& cp : m_ws.m_canvasProfiles) {
        if (cp.id == id) {
            cp.templateInfo = std::move(info);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Output profile palette
// ---------------------------------------------------------------------------

std::string WorkspaceEditor::addOutputProfile(Models::OutputProfile p)
{
    p.id = makeUniqueOutputProfileId(m_ws.m_outputProfiles); // always a user id, never a preset id
    m_ws.m_outputProfiles.push_back(std::move(p));
    return m_ws.m_outputProfiles.back().id;
}

bool WorkspaceEditor::removeOutputProfile(const std::string& id)
{
    const auto before = m_ws.m_outputProfiles.size();
    m_ws.m_outputProfiles.erase(
        std::remove_if(m_ws.m_outputProfiles.begin(), m_ws.m_outputProfiles.end(),
                       [&id](const Models::OutputProfile& op) { return op.id == id; }),
        m_ws.m_outputProfiles.end());
    return m_ws.m_outputProfiles.size() != before;
}

WorkspaceRepairReport WorkspaceEditor::replaceOutputProfiles(
    std::vector<Models::OutputProfile> incoming)
{
    // Presets live in code and must never be persisted; drop any that were handed back.
    incoming.erase(
        std::remove_if(incoming.begin(), incoming.end(),
                       [](const Models::OutputProfile& op) {
                           return Models::outputPresetDefById(op.id) != nullptr;
                       }),
        incoming.end());

    m_ws.m_outputProfiles = std::move(incoming);

    WorkspaceRepairReport report;
    mintMissingProfileIds(m_ws);
    deduplicateIds(
        m_ws.m_outputProfiles,
        [](const auto& existing) { return makeUniqueOutputProfileId(existing); },
        report.outputProfiles);
    return report;
}

// ---------------------------------------------------------------------------
// Cross-workspace import (profile portability)
// ---------------------------------------------------------------------------

ImportProfilesReport WorkspaceEditor::importProfiles(std::vector<Models::CanvasProfile> canvas,
                                                     std::vector<Models::OutputProfile> output)
{
    ImportProfilesReport report;

    // Canvas: templateInfo is workspace-local (a path relative to the source), so drop it — an
    // imported profile has no template here yet. addCanvasProfile mints a fresh unique id.
    report.canvasIds.reserve(canvas.size());
    for (auto& cp : canvas) {
        cp.templateInfo = {};
        report.canvasIds.push_back(addCanvasProfile(std::move(cp)));
    }

    // Output: skip presets (code-defined, resolved from the catalogue — never a user copy on import),
    // mirroring replaceOutputProfiles. addOutputProfile mints a fresh user id for the rest.
    report.outputIds.reserve(output.size());
    for (auto& op : output) {
        if (Models::outputPresetDefById(op.id))
            continue;
        report.outputIds.push_back(addOutputProfile(std::move(op)));
    }

    return report;
}

// ---------------------------------------------------------------------------
// Projects
// ---------------------------------------------------------------------------

Models::ProjectItem& WorkspaceEditor::addProject(std::string name)
{
    std::vector<std::string> taken;
    taken.reserve(m_ws.projectItems.size());
    for (const auto& p : m_ws.projectItems)
        taken.push_back(p.uid);

    Models::ProjectItem project;
    project.name = std::move(name);
    project.uid  = makeUniqueId("proj", taken);

    m_ws.projectItems.push_back(std::move(project));
    return m_ws.projectItems.back();
}

Models::ProjectItem& WorkspaceEditor::duplicateProject(const Models::ProjectItem& source,
                                                       std::string newName)
{
    std::vector<std::string> taken;
    taken.reserve(m_ws.projectItems.size());
    for (const auto& p : m_ws.projectItems)
        taken.push_back(p.uid);

    Models::ProjectItem copy;
    copy.name           = std::move(newName);
    copy.uid            = makeUniqueId("proj", taken); // fresh, workspace-unique; the source keeps its own
    copy.inputDirectory = source.inputDirectory;       // input-side hint (last-scanned folder); harmless

    // Copy only the input *files* — path, strip order and the reusable cache thumbnail. Everything
    // that describes a past render (sha256, status, contributesTo, canvasProfileId/fingerprint,
    // lastProcessed) is deliberately left at its Pending default: this is a new project that has not
    // rendered, not a clone of the source's results.
    auto& dstInputs = copy.getInputImages();
    dstInputs.reserve(source.getInputImages().size());
    for (const auto& src : source.getInputImages()) {
        Models::InputFile f;
        f.filePath      = src.filePath;
        f.order         = src.order;
        f.thumbnailPath = src.thumbnailPath; // shared .platemaker-cache entry, safe to reuse
        dstInputs.push_back(std::move(f));   // uid left empty → minted below
    }
    copy.ensureUniqueFileUids(); // fresh project-local input uids

    // Carry the profile configuration the source rendered with, so the copy matches pages the same way
    // (WorkspaceEditor is a friend of ProjectItem, like relinkProfileId above). The output profile is
    // the field the multi-publisher workflow then changes per copy; the canvas links keep the crop/scale
    // behaviour identical. Both ids resolve against the same workspace palettes, so no re-validation is
    // needed. Output directory and the output slice list are intentionally NOT copied.
    copy.m_canvasProfileIds = source.m_canvasProfileIds;
    copy.m_outputProfileId  = source.m_outputProfileId;

    copy.rebuildLookupTables(); // no outputs yet → empty tables, but keep the invariant explicit

    m_ws.projectItems.push_back(std::move(copy));
    return m_ws.projectItems.back();
}

// ---------------------------------------------------------------------------
// Project ↔ profile links
// ---------------------------------------------------------------------------

bool WorkspaceEditor::addCanvasProfileToProject(
    Models::ProjectItem& project, const std::string& profileId)
{
    return project.addCanvasProfile(m_ws.m_canvasProfiles, profileId);
}

bool WorkspaceEditor::removeCanvasProfileFromProject(
    Models::ProjectItem& project, const std::string& profileId)
{
    auto&      ids    = project.m_canvasProfileIds; // private; WorkspaceEditor is a friend of ProjectItem
    const auto before = ids.size();
    ids.erase(std::remove(ids.begin(), ids.end(), profileId), ids.end());
    return ids.size() != before;
}

bool WorkspaceEditor::setProjectOutputProfile(
    Models::ProjectItem& project, const std::string& outputProfileId)
{
    // Empty = "use the workspace default"; a non-empty id must resolve to a user profile or a
    // baked-in preset.
    if (!outputProfileId.empty() && !Models::resolveOutputProfile(m_ws, outputProfileId))
        return false;

    project.m_outputProfileId = outputProfileId; // private; WorkspaceEditor is a friend of ProjectItem
    return true;
}

// ---------------------------------------------------------------------------
// Load path
// ---------------------------------------------------------------------------

void WorkspaceEditor::installLoaded(std::vector<Models::CanvasProfile>&& canvas,
                                    std::vector<Models::OutputProfile>&& output,
                                    WorkspaceRepairReport&               report)
{
    report = WorkspaceRepairReport{};

    m_ws.m_canvasProfiles = std::move(canvas);
    m_ws.m_outputProfiles = std::move(output);

    // Mint first, then deduplicate.  A file can arrive needing both: profiles saved without an
    // id at all, and profiles that share one (ids used to be a millisecond timestamp).
    mintMissingProfileIds(m_ws);

    deduplicateIds(
        m_ws.m_canvasProfiles,
        [](const auto& existing) { return makeUniqueCanvasProfileId(existing); },
        report.canvasProfiles);

    deduplicateIds(
        m_ws.m_outputProfiles,
        [](const auto& existing) { return makeUniqueOutputProfileId(existing); },
        report.outputProfiles);

    // Presets last: the collapse pass hands out relinks and drops copies, so it must run on a
    // list that is already free of duplicate ids.
    migrateOutputProfilePresets(m_ws);
}

// ---------------------------------------------------------------------------
// Snapshot / restore (workspace-level metadata) — undo/redo support
// ---------------------------------------------------------------------------

std::string WorkspaceEditor::snapshotMeta() const
{
    // Project roster: uid + name only (no contents — those are per-project snapshots).
    nlohmann::json projects = nlohmann::json::array();
    for (const auto& pi : m_ws.projectItems)
        projects.push_back(nlohmann::json{{"uid", pi.uid}, {"name", pi.name}});

    // Output profiles: filter presets, mirroring the workspace codec (they are never persisted).
    nlohmann::json outputs = nlohmann::json::array();
    for (const auto& op : m_ws.outputProfiles())
        if (!Models::outputPresetDefById(op.id))
            outputs.push_back(op);

    const nlohmann::json j{
        {"canvasProfiles", m_ws.canvasProfiles()},
        {"outputProfiles", outputs},
        {"projects",       projects}
    };
    return j.dump();
}

void WorkspaceEditor::restoreMeta(const std::string& snapshot)
{
    const nlohmann::json j = nlohmann::json::parse(snapshot);

    // Reinstall the palettes through the validated setters: ids preserved, presets stripped, and
    // (for canvas) templateInfo carried from the current profile of the same id. The carry heuristic
    // is not exact, so re-apply each snapshot's templateInfo explicitly afterwards — this makes an
    // undo of template generation/deletion restore the exact recorded value.
    auto canvas = j.at("canvasProfiles").get<std::vector<Models::CanvasProfile>>();
    replaceCanvasProfiles(canvas);
    for (const auto& cp : canvas)
        setCanvasProfileTemplateInfo(cp.id, cp.templateInfo);

    replaceOutputProfiles(j.at("outputProfiles").get<std::vector<Models::OutputProfile>>());

    // Restore project names by uid. The roster itself is not changed here (add/remove is not
    // undoable), so a uid the snapshot lists but that no longer exists is simply skipped, and a
    // project not in the snapshot is left as-is.
    std::unordered_map<std::string, std::string> nameByUid;
    for (const auto& jp : j.at("projects"))
        nameByUid.emplace(jp.at("uid").get<std::string>(), jp.at("name").get<std::string>());
    for (auto& pi : m_ws.projectItems) {
        const auto it = nameByUid.find(pi.uid);
        if (it != nameByUid.end())
            pi.name = it->second;
    }
}

} // namespace Platemaker::Infrastructure
