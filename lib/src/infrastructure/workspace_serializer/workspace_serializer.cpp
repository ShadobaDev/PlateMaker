/**
 * \file lib/src/infrastructure/workspace_serializer/workspace_serializer.cpp
 * \brief WorkspaceSerializer implementation — JSON round-trip for all model types.
 *
 * All nlohmann/json from_json / to_json overloads for the Platemaker::Models
 * types are defined here (in the Models namespace for ADL) rather than in
 * individual model headers, keeping the model headers free of third-party
 * dependencies.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>

#include "infrastructure/model_json/model_json.hpp"   // the shared Models JSON codec

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The nlohmann/json to_json / from_json overloads for every Platemaker::Models type used to live
// here; they now live in infrastructure/model_json/model_json.{hpp,cpp} so the serializer (file
// I/O) and the editor facades (partial snapshot/restore for undo) share one codec.

// ---------------------------------------------------------------------------
// WorkspaceSerializer implementation
// ---------------------------------------------------------------------------
namespace Platemaker::Infrastructure {

namespace {
    /// Current on-disk schema version.  Increment on every breaking schema change.
    constexpr int k_currentVersion = 2;

// The identifier-repair helpers (mintMissingProfileIds / deduplicateIds /
// migrateOutputProfilePresets / relinkProfileId) that used to live here now belong to
// WorkspaceEditor, so load() and every in-session edit run one copy of the rules.  load()
// reaches them via WorkspaceEditor::installLoaded() below.

// Reconstructs the input-composition baseline (inputOrderAtRender) of a project that was rendered
// before that field existed, so a reorder done under an older build is still caught. The outputs'
// sourceMap records which input fed each slice, in vertical order; scanning the slices in name order
// and taking each source path's first appearance recovers the render-time strip order. Paths are
// mapped to the current input uid (the baseline is uid-keyed); a source path that is no longer an
// input means the composition already changed, so its path is kept as a token that cannot match any
// current uid — the comparison then correctly reports a change. Runs only when there is no baseline
// yet and outputs exist; an unchanged project reconstructs its own current order and stays up to date.
void backfillInputOrderBaseline(Models::ProjectItem& pi)
{
    if (!pi.inputOrderAtRender.empty() || pi.getOutputImages().empty())
        return;

    std::unordered_map<std::string, std::string> pathToUid;
    for (const auto& inf : pi.getInputImages())
        pathToUid.emplace(inf.filePath, inf.uid);

    std::vector<const Models::OutputFile*> outs;
    outs.reserve(pi.getOutputImages().size());
    for (const auto& out : pi.getOutputImages())
        outs.push_back(&out);
    std::stable_sort(outs.begin(), outs.end(),
                     [](const Models::OutputFile* a, const Models::OutputFile* b) {
                         return a->fileName < b->fileName;
                     });

    std::vector<std::string> order;
    std::unordered_set<std::string> seen;
    for (const auto* out : outs)
        for (const auto& seg : out->sourceMap)
            if (seen.insert(seg.sourceFilePath).second) {
                const auto it = pathToUid.find(seg.sourceFilePath);
                order.push_back(it != pathToUid.end() ? it->second : seg.sourceFilePath);
            }

    // Nothing to compare against (no provenance) → leave empty; sanitize() then skips the axis.
    if (!order.empty())
        pi.inputOrderAtRender = std::move(order);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

Models::Workspace WorkspaceSerializer::load(const std::string& filePath) const
{
    WorkspaceRepairReport discarded;
    return load(filePath, discarded);
}

Models::Workspace WorkspaceSerializer::load(const std::string&     filePath,
                                            WorkspaceRepairReport& report) const
{
    report = WorkspaceRepairReport{};

    // Through utf8ToPath(), matching save() below. The two used to disagree — save() opened
    // via an fs::path (converted correctly) while load() passed the narrow string straight to
    // fopen() (read as ANSI) — so a workspace under a non-ASCII path could be written once
    // and then never reopened.
    std::ifstream file(utf8ToPath(filePath));
    if (!file.is_open()) {
        throw std::runtime_error(
            "WorkspaceSerializer::load() — cannot open '" + filePath + "'");
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            "WorkspaceSerializer::load() — JSON parse error in '" +
            filePath + "': " + e.what());
    }

    if (!j.contains("version") || !j.at("version").is_number_integer()) {
        throw std::runtime_error(
            "WorkspaceSerializer::load() — missing or invalid 'version' field in '" +
            filePath + "'");
    }

    const int fileVersion = j.at("version").get<int>();

    Models::Workspace                  workspace;
    std::vector<Models::CanvasProfile> canvasProfiles;
    std::vector<Models::OutputProfile> outputProfiles;
    try {
        workspace = j.get<Models::Workspace>();
        // The profile palettes are parsed here rather than in from_json(Workspace): the vectors
        // are private and only WorkspaceEditor may install them, which is what enforces the
        // load-time invariants below.
        j.at("canvasProfiles").get_to(canvasProfiles);
        j.at("outputProfiles").get_to(outputProfiles);

        // Install each project's link fields. They are private on ProjectItem and from_json leaves
        // them empty by design; WorkspaceSerializer is a friend, so it writes them here — the friend
        // path that stops a raw consumer from setting an unvalidated outputProfileId or bypassing the
        // canvas dimension guard. The array order matches workspace.projectItems (nlohmann preserves
        // it). This runs before installLoaded() below, whose relink pass rewrites these references.
        if (j.contains("projectItems")) {
            const auto&       jProjects = j.at("projectItems");
            const std::size_t n = std::min(workspace.projectItems.size(), jProjects.size());
            for (std::size_t i = 0; i < n; ++i) {
                workspace.projectItems[i].m_canvasProfileIds =
                    jProjects[i].value("canvasProfileIds", std::vector<std::string>{});
                workspace.projectItems[i].m_outputProfileId =
                    jProjects[i].value("outputProfileId", std::string{});
            }
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(
            "WorkspaceSerializer::load() — schema error in '" +
            filePath + "': " + e.what());
    }

    if (fileVersion < k_currentVersion) {
        migrate(workspace, fileVersion);
    }

    // Install the parsed palettes and run the identifier-repair pass (mint → deduplicate → drop
    // persisted presets).  This is the single copy of the rules, shared with every WorkspaceEditor
    // edit, so a file read from disk and a workspace edited in memory obey the same invariants.
    // Runs for ANY file version: a file can arrive needing both a mint (profiles saved without an
    // id) and a dedup (ids used to be a millisecond timestamp, so several came out identical).
    WorkspaceEditor(workspace).installLoaded(
        std::move(canvasProfiles), std::move(outputProfiles), report);

    // Repair identifiers and rebuild runtime tables for every project. Local uids (project, input,
    // output) are minted where missing — a workspace written under the old "uuid" key arrives with them
    // empty (that key is no longer read) — or where duplicated (the historical position-derived input
    // ids could collide across re-scans). The lookup tables are not serialised, so they are always
    // reconstructed after loading.
    std::vector<std::string> takenProjectUids;
    takenProjectUids.reserve(workspace.projectItems.size());
    for (auto& pi : workspace.projectItems) {
        if (pi.uid.empty() ||
            std::find(takenProjectUids.begin(), takenProjectUids.end(), pi.uid) != takenProjectUids.end())
            pi.uid = makeUniqueId("proj", takenProjectUids);
        takenProjectUids.push_back(pi.uid);

        pi.ensureUniqueFileUids();
        pi.rebuildLookupTables();

        // A project rendered before the input-composition axis existed carries no baseline; rebuild it
        // from output provenance so a reorder done under an older build is not silently missed.
        backfillInputOrderBaseline(pi);
    }

    return workspace;
}

// ---------------------------------------------------------------------------
// save (atomic write: temp file → rename)
// ---------------------------------------------------------------------------

std::string WorkspaceSerializer::serialize(
    const Models::Workspace& workspace) const
{
    const nlohmann::json j = workspace;
    return j.dump(4); // pretty-print, 4-space indent
}

void WorkspaceSerializer::save(
    const Models::Workspace& workspace,
    const std::string&       filePath) const
{
    namespace fs = std::filesystem;

    const std::string text = serialize(workspace);

    // Write to a sibling temp file then rename for near-atomic replacement.
    // The appended name goes back through utf8ToPath() as well: operator/ with a narrow
    // string would rebuild a path the ambiguous way, reintroducing the bug on the temp file.
    const fs::path finalPath = utf8ToPath(filePath);
    const fs::path tmpPath   =
        finalPath.parent_path() / utf8ToPath(pathToUtf8(finalPath.filename()) + ".tmp");

    {
        std::ofstream tmp(tmpPath, std::ios::out | std::ios::trunc);
        if (!tmp.is_open()) {
            throw std::runtime_error(
                "WorkspaceSerializer::save() — cannot open temp file '" +
                pathToUtf8(tmpPath) + "' for writing");
        }
        tmp << text;
        if (!tmp.good()) {
            throw std::runtime_error(
                "WorkspaceSerializer::save() — write error for '" +
                pathToUtf8(tmpPath) + "'");
        }
    } // tmp closed and flushed here

    std::error_code ec;
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        // Fallback for cross-device rename (uncommon but possible on Windows).
        fs::copy_file(tmpPath, finalPath,
                      fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(tmpPath, ec);
        if (ec) {
            throw std::runtime_error(
                "WorkspaceSerializer::save() — cannot move temp file to '" +
                filePath + "': " + ec.message());
        }
    }
}

// ---------------------------------------------------------------------------
// migrate — schema migration chain
// ---------------------------------------------------------------------------

void WorkspaceSerializer::migrate(
    Models::Workspace& workspace,
    int                fromVersion) const
{
    // v1 → v2: removed activeCanvasProfileName / activeOutputProfileName from Workspace;
    //           added OutputProfile::id (back-compat: empty string for profiles missing it);
    //           added ProjectItem::canvasProfileIds and outputProfileId (default: empty).
    //           All handled in from_json with j.contains() guards — no in-place fixup needed.
    if (fromVersion < 2) {
        workspace.version = k_currentVersion;
        // Ids for profiles deserialised without one are minted by mintMissingProfileIds()
        // in load(), which runs for every file version — so there is nothing to do here.
        // It also relinks the legacy "op-<name>" / "cp-<name>" references that a v1 file
        // may carry, which this step could not do on its own.
    }
}

} // namespace Platemaker::Infrastructure
