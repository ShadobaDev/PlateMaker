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

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// JSON serialisation helpers for Platemaker::Models types.
//
// Defined in the Platemaker::Models namespace so nlohmann/json can discover
// them via argument-dependent lookup (ADL) when serialising nested objects.
//
// Ordering matters: each serialiser must appear before any serialiser that
// calls it (e.g. Workspace's to_json calls CanvasProfile's to_json).
// ---------------------------------------------------------------------------
namespace Platemaker::Models {

// --- RGBA ---
void to_json(nlohmann::json& j, const RGBA& v) {
    j = nlohmann::json{{"r", v.r}, {"g", v.g}, {"b", v.b}, {"a", v.a}};
}
void from_json(const nlohmann::json& j, RGBA& v) {
    j.at("r").get_to(v.r);
    j.at("g").get_to(v.g);
    j.at("b").get_to(v.b);
    j.at("a").get_to(v.a);
}

// --- Size ---
void to_json(nlohmann::json& j, const Size& v) {
    j = nlohmann::json{{"width", v.width}, {"height", v.height}};
}
void from_json(const nlohmann::json& j, Size& v) {
    j.at("width").get_to(v.width);
    j.at("height").get_to(v.height);
}

// --- Margins ---
void to_json(nlohmann::json& j, const Margins& v) {
    j = nlohmann::json{
        {"top", v.top}, {"right", v.right},
        {"bottom", v.bottom}, {"left", v.left}};
}
void from_json(const nlohmann::json& j, Margins& v) {
    j.at("top").get_to(v.top);
    j.at("right").get_to(v.right);
    j.at("bottom").get_to(v.bottom);
    j.at("left").get_to(v.left);
}

// --- Enumerations (string-based for human-readable JSON) ---

NLOHMANN_JSON_SERIALIZE_ENUM(LastSlicePolicy, {
    {LastSlicePolicy::Crop,     "Crop"},
    {LastSlicePolicy::PadWhite, "PadWhite"},
    {LastSlicePolicy::KeepAsIs, "KeepAsIs"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OutputFormat, {
    {OutputFormat::PNG,  "PNG"},
    {OutputFormat::JPEG, "JPEG"},
    {OutputFormat::WebP, "WebP"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(JpegSubsampling, {
    {JpegSubsampling::YUV_444, "YUV_444"},
    {JpegSubsampling::YUV_422, "YUV_422"},
    {JpegSubsampling::YUV_420, "YUV_420"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(FileStatus, {
    {FileStatus::Pending,        "Pending"},
    {FileStatus::Processed,      "Processed"},
    {FileStatus::Modified,       "Modified"},
    {FileStatus::Missing,        "Missing"},
    {FileStatus::Desynchronized, "Desynchronized"},
    {FileStatus::Done,           "Done"}
})

// --- JpegOptions ---
void to_json(nlohmann::json& j, const JpegOptions& v) {
    j = nlohmann::json{
        {"quality",     v.quality},
        {"subsampling", v.subsampling},
        {"optimize",    v.optimize},
        {"progressive", v.progressive}
    };
}
void from_json(const nlohmann::json& j, JpegOptions& v) {
    j.at("quality").get_to(v.quality);
    j.at("subsampling").get_to(v.subsampling);
    j.at("optimize").get_to(v.optimize);
    j.at("progressive").get_to(v.progressive);
}

// --- PngOptions ---
void to_json(nlohmann::json& j, const PngOptions& v) {
    j = nlohmann::json{
        {"compression", v.compression},
        {"interlaced",  v.interlaced}
    };
}
void from_json(const nlohmann::json& j, PngOptions& v) {
    if (j.contains("compression")) j.at("compression").get_to(v.compression);
    if (j.contains("interlaced"))  j.at("interlaced").get_to(v.interlaced);
}

// --- WebpOptions ---
void to_json(nlohmann::json& j, const WebpOptions& v) {
    j = nlohmann::json{
        {"quality",  v.quality},
        {"lossless", v.lossless},
        {"effort",   v.effort}
    };
}
void from_json(const nlohmann::json& j, WebpOptions& v) {
    if (j.contains("quality"))  j.at("quality").get_to(v.quality);
    if (j.contains("lossless")) j.at("lossless").get_to(v.lossless);
    if (j.contains("effort"))   j.at("effort").get_to(v.effort);
}

// --- CanvasTemplateInfo ---
void to_json(nlohmann::json& j, const CanvasTemplateInfo& v) {
    j = nlohmann::json{
        {"path",        v.path},
        {"fingerprint", v.fingerprint},
        {"generatedAt", v.generatedAt}
    };
}
void from_json(const nlohmann::json& j, CanvasTemplateInfo& v) {
    if (j.contains("path"))        j.at("path").get_to(v.path);
    if (j.contains("fingerprint")) j.at("fingerprint").get_to(v.fingerprint);
    if (j.contains("generatedAt")) j.at("generatedAt").get_to(v.generatedAt);
}

// --- CanvasProfile ---
void to_json(nlohmann::json& j, const CanvasProfile& v) {
    j = nlohmann::json{
        {"id",                     v.id},
        {"name",                   v.name},
        {"canvasSize",             v.canvasSize},
        {"margins",                v.margins},
        {"visualColour",           v.visualColour},
        {"backgroundColour",       v.backgroundColour},
        {"hintUserSafeAreaSelect", v.hintUserSafeAreaSelect},
        {"templateInfo",           v.templateInfo}
    };
}
void from_json(const nlohmann::json& j, CanvasProfile& v) {
    // Left empty when absent (pre-id workspaces); load() mints a unique id and
    // relinks the legacy "cp-<name>" references.  See mintMissingProfileIds().
    if (j.contains("id")) j.at("id").get_to(v.id);
    j.at("name").get_to(v.name);
    j.at("canvasSize").get_to(v.canvasSize);
    j.at("margins").get_to(v.margins);
    if (j.contains("visualColour"))
        j.at("visualColour").get_to(v.visualColour);
    if (j.contains("backgroundColour"))
        j.at("backgroundColour").get_to(v.backgroundColour);
    if (j.contains("hintUserSafeAreaSelect"))
        j.at("hintUserSafeAreaSelect").get_to(v.hintUserSafeAreaSelect);
    if (j.contains("templateInfo"))
        j.at("templateInfo").get_to(v.templateInfo);
}

// --- OutputProfile ---
void to_json(nlohmann::json& j, const OutputProfile& v) {
    j = nlohmann::json{
        {"id",              v.id},
        {"name",            v.name},
        {"targetWidth",     v.targetWidth},
        {"sliceHeight",     v.sliceHeight},
        {"lastSlicePolicy", v.lastSlicePolicy},
        {"outputFormat",    v.outputFormat},
        {"jpegOptions",     v.jpegOptions},
        {"pngOptions",      v.pngOptions},
        {"webpOptions",     v.webpOptions},
        {"startIndex",      v.startIndex}
    };
}
void from_json(const nlohmann::json& j, OutputProfile& v) {
    if (j.contains("id")) j.at("id").get_to(v.id); // back-compat: missing in v1
    j.at("name").get_to(v.name);
    j.at("targetWidth").get_to(v.targetWidth);
    j.at("sliceHeight").get_to(v.sliceHeight);
    j.at("lastSlicePolicy").get_to(v.lastSlicePolicy);
    j.at("outputFormat").get_to(v.outputFormat);
    j.at("jpegOptions").get_to(v.jpegOptions);
    if (j.contains("pngOptions"))  j.at("pngOptions").get_to(v.pngOptions);   // additive
    if (j.contains("webpOptions")) j.at("webpOptions").get_to(v.webpOptions); // additive
    j.at("startIndex").get_to(v.startIndex);
}


// --- InputFile ---
void to_json(nlohmann::json& j, const InputFile& v) {
    j = nlohmann::json{
        {"uuid", v.uuid},
        {"filePath", v.filePath},
        {"sha256", v.sha256},
        {"order", v.order},
        {"thumbnailPath", v.thumbnailPath},
        {"status", v.status},
        {"lastProcessed", v.lastProcessed},
        {"contributesTo", v.contributesTo},
        {"canvasProfileId", v.canvasProfileId},
        {"canvasFingerprint", v.canvasFingerprint}
    };
}

void from_json(const nlohmann::json& j, InputFile& v) {
    j.at("uuid").get_to(v.uuid);
    j.at("filePath").get_to(v.filePath);
    j.at("sha256").get_to(v.sha256);
    j.at("order").get_to(v.order);
    j.at("thumbnailPath").get_to(v.thumbnailPath);
    j.at("status").get_to(v.status);
    j.at("lastProcessed").get_to(v.lastProcessed);
    j.at("contributesTo").get_to(v.contributesTo);
    // additive — absent in workspaces written before canvas-profile staleness was
    // tracked; an empty fingerprint simply means "no baseline for this page".
    if (j.contains("canvasProfileId"))   j.at("canvasProfileId").get_to(v.canvasProfileId);
    if (j.contains("canvasFingerprint")) j.at("canvasFingerprint").get_to(v.canvasFingerprint);
}

// --- SourceSegment ---
void to_json(nlohmann::json& j, const SourceSegment& v) {
    j = nlohmann::json{
        {"sourceFilePath", v.sourceFilePath},
        {"srcY", v.srcY},
        {"height", v.height}
    };
}

void from_json(const nlohmann::json& j, SourceSegment& v) {
    j.at("sourceFilePath").get_to(v.sourceFilePath);
    j.at("srcY").get_to(v.srcY);
    j.at("height").get_to(v.height);
}

// --- OutputFile ---
void to_json(nlohmann::json& j, const OutputFile& v) {
    j = nlohmann::json{
        {"uuid", v.uuid},
        {"fileName", v.fileName},
        {"sha256", v.sha256},
        {"sourceMap", v.sourceMap},
        {"status", v.status}
    };
}

void from_json(const nlohmann::json& j, OutputFile& v) {
    j.at("uuid").get_to(v.uuid);
    j.at("fileName").get_to(v.fileName);
    j.at("sha256").get_to(v.sha256);
    j.at("sourceMap").get_to(v.sourceMap);
    j.at("status").get_to(v.status);
}

// --- ProjectItem ---
void to_json(nlohmann::json& j, const ProjectItem& v) {
    j = nlohmann::json{
        {"name",             v.name},
        {"uuid",             v.uuid},
        {"inputDirectory",   v.inputDirectory},
        {"canvasProfileIds", v.canvasProfileIds},
        {"outputProfileId",  v.outputProfileId},
        {"outputSignature",  v.outputSignature},
        {"canvasProfileIdsAtRender", v.canvasProfileIdsAtRender},
        {"inputFiles",       v.getInputImages()},
        {"outputFiles",      v.getOutputImages()},
        {"outputDirectory",  v.getOutputDirectory()}
    };
}
void from_json(const nlohmann::json& j, ProjectItem& v) {
    if (j.contains("name"))             j.at("name").get_to(v.name);
    if (j.contains("uuid"))             j.at("uuid").get_to(v.uuid);
    if (j.contains("inputDirectory"))   j.at("inputDirectory").get_to(v.inputDirectory);
    if (j.contains("canvasProfileIds")) j.at("canvasProfileIds").get_to(v.canvasProfileIds);
    if (j.contains("outputProfileId"))  j.at("outputProfileId").get_to(v.outputProfileId);
    if (j.contains("outputSignature"))  j.at("outputSignature").get_to(v.outputSignature);
    if (j.contains("canvasProfileIdsAtRender"))
        j.at("canvasProfileIdsAtRender").get_to(v.canvasProfileIdsAtRender);
    j.at("inputFiles").get_to(v.getInputImages());
    j.at("outputFiles").get_to(v.getOutputImages());
    j.at("outputDirectory").get_to(v.getOutputDirectory());
}

// --- Workspace ---
void to_json(nlohmann::json& j, const Workspace& v) {
    j = nlohmann::json{
        {"version",         v.version},
        {"canvasProfiles",  v.canvasProfiles},
        {"outputProfiles",  v.outputProfiles},
        {"projectItems",    v.projectItems},
        {"outputDirectory", v.outputDirectory},
        {"stripDirty",      v.stripDirty}
    };
}

void from_json(const nlohmann::json& j, Workspace& v) {
    // Detect legacy workspace format (pre-ProjectItem refactoring).
    if (j.contains("pages") || j.contains("processedFiles")) {
        throw std::runtime_error(
            "Workspace schema is incompatible with this version of Platemaker.\n"
            "  The workspace was created with an older version that stored 'pages'\n"
            "  and 'processedFiles' directly in the workspace.\n"
            "  Please create a new workspace with:\n"
            "    platemaker workspace create --output <new-workspace.platemaker.json>");
    }

    j.at("version").get_to(v.version);
    j.at("canvasProfiles").get_to(v.canvasProfiles);
    j.at("outputProfiles").get_to(v.outputProfiles);
    // activeCanvasProfileName / activeOutputProfileName removed in schema v2 — silently ignored.
    if (j.contains("projectItems"))    j.at("projectItems").get_to(v.projectItems);
    if (j.contains("outputDirectory")) j.at("outputDirectory").get_to(v.outputDirectory);
    if (j.contains("stripDirty"))      j.at("stripDirty").get_to(v.stripDirty);
}

} // namespace Platemaker::Models

// ---------------------------------------------------------------------------
// WorkspaceSerializer implementation
// ---------------------------------------------------------------------------
namespace Platemaker::Infrastructure {

namespace {
    /// Current on-disk schema version.  Increment on every breaking schema change.
    constexpr int k_currentVersion = 2;

// ---------------------------------------------------------------------------
// Identifier repair helpers
//
// Both run on every load, for any file version.  Order matters: mint first, then
// deduplicate — two id-less profiles sharing a name used to derive the same legacy
// id, so minting after the dedup pass would leave that collision in place.
// ---------------------------------------------------------------------------

/**
 * \brief Rewrites every project reference to \p oldId so it points at \p newId instead.
 *
 * Covers all four places a profile id is stored on a project: the assigned canvas
 * profiles, the output profile, the canvas baseline recorded at render time, and the
 * per-input profile the page was rendered with.
 */
void relinkProfileId(Models::Workspace&  workspace,
                     const std::string&  oldId,
                     const std::string&  newId)
{
    for (auto& pi : workspace.projectItems) {
        for (auto& id : pi.canvasProfileIds)
            if (id == oldId) id = newId;

        for (auto& id : pi.canvasProfileIdsAtRender)
            if (id == oldId) id = newId;

        if (pi.outputProfileId == oldId)
            pi.outputProfileId = newId;

        for (auto& inf : pi.getInputImages())
            if (inf.canvasProfileId == oldId)
                inf.canvasProfileId = newId;
    }
}

/**
 * \brief Gives a random unique id to every profile saved without one, migrating the
 *        references that used to rely on the id being derived from the name.
 *
 * Pre-id workspaces (and, until 0.2.1, any profile a GUI saved with an empty id) had
 * their id computed as \c "cp-<name>" / \c "op-<name>".  That was a second, parallel
 * identity scheme and it was not unique either — two profiles sharing a name shared an
 * id.  Since the old form is *deterministic*, we can compute what a profile's id would
 * have been and relink exactly those references, which turns the name-derived scheme
 * into a one-off migration instead of a permanent fixture.
 *
 * Not reported: this is unambiguous bookkeeping, not a collision the user should hear about.
 */
void mintMissingProfileIds(Models::Workspace& workspace)
{
    for (auto& cp : workspace.canvasProfiles) {
        if (!cp.id.empty()) continue;
        cp.id = makeUniqueCanvasProfileId(workspace.canvasProfiles);
        relinkProfileId(workspace, "cp-" + cp.name, cp.id);
    }

    for (auto& op : workspace.outputProfiles) {
        if (!op.id.empty()) continue;
        op.id = makeUniqueOutputProfileId(workspace.outputProfiles);
        relinkProfileId(workspace, "op-" + op.name, op.id);
    }
}

/**
 * \brief Restores the invariant "a preset identifier means exactly the preset's settings".
 *
 * Preset ids are shared by every workspace, which is what makes a preset recognisable across
 * files and app updates — but only as long as the id cannot come to mean something else.
 * Three cases, all silent because none of them is ambiguous:
 *
 * - **Adoption.** A profile carrying the legacy id \c "op-Webtoon Standard" is given the
 *   canonical preset id, *provided its settings still match the preset*. If the user changed
 *   them, it is left exactly as it is: it is their profile, not the preset, and promoting it
 *   would make the shared id assert something false.
 * - **Fork.** A profile carrying a preset id whose settings do **not** match (edited by an
 *   older build, or by hand in the JSON) is given a fresh random id. Its settings are
 *   untouched — overwriting them with the canonical ones would destroy the user's work.
 * - **Presence.** Any preset missing from the workspace is appended.
 *
 * The three compose: a diverged profile is forked, which frees the canonical id, and the
 * presence pass then puts the real preset back beside it. The user keeps their customised
 * profile and regains the preset, without being asked anything.
 *
 * \note Appends, never prepends. resolveOutputProfile() falls back to \c outputProfiles.front()
 *       when a project has no assignment, so inserting at the front would silently change
 *       which profile existing workspaces render with.
 */
void enforceOutputProfilePresets(Models::Workspace& workspace)
{
    const auto presets = Models::outputProfilePresets();

    // Adoption: a profile that *is* the preset takes the canonical id.
    //
    // Matched on the legacy name-derived id ("op-Webtoon Standard", the form the GUI wrote)
    // or on the name, and in both cases only when the settings still match — a profile the
    // user changed is theirs, and promoting it would make the shared id assert something
    // false. The name arm matters because pre-id workspaces reach this point already carrying
    // a freshly minted random id; without it they would keep that id and the presence pass
    // would append a *second*, identical "Webtoon Standard" beside it.
    for (auto& op : workspace.outputProfiles) {
        for (const auto& preset : presets) {
            const bool looksLikeThisPreset =
                op.id == "op-" + preset.name || op.name == preset.name;
            if (!looksLikeThisPreset) continue;
            if (Models::outputProfileSignature(op) != Models::outputProfileSignature(preset))
                continue;

            // Never mint a duplicate: if something already holds the canonical id, leave
            // this one alone and let the presence pass see the preset as present.
            const bool idTaken = std::any_of(
                workspace.outputProfiles.begin(), workspace.outputProfiles.end(),
                [&](const Models::OutputProfile& other) { return other.id == preset.id; });
            if (idTaken) continue;

            const std::string oldId = op.id;
            op.id = preset.id;
            relinkProfileId(workspace, oldId, op.id);
            break;
        }
    }

    // Fork: carries a preset id but is no longer that preset.
    for (auto& op : workspace.outputProfiles) {
        const auto preset = Models::outputProfilePresetById(op.id);
        if (!preset) continue;
        if (Models::outputProfileSignature(op) == Models::outputProfileSignature(*preset))
            continue;

        const std::string oldId = op.id;
        op.id = makeUniqueOutputProfileId(workspace.outputProfiles);
        relinkProfileId(workspace, oldId, op.id);
    }

    // Presence: presets are always in the set.
    for (const auto& preset : presets) {
        const bool present = std::any_of(
            workspace.outputProfiles.begin(), workspace.outputProfiles.end(),
            [&](const Models::OutputProfile& op) { return op.id == preset.id; });

        if (!present)
            workspace.outputProfiles.push_back(preset);
    }
}

/**
 * \brief Breaks up shared identifiers so every profile is reachable again.
 *
 * The first profile holding an id keeps it — that way every existing project reference
 * still resolves, and to the same profile it resolved to before.  Later duplicates get a
 * fresh id and become unassigned, which is what puts them back in the "assign a profile"
 * list they had silently dropped out of.
 *
 * Deliberately does **not** touch project references to the duplicated id.  We cannot know
 * which of the two profiles a project really rendered with, and guessing would be worse than
 * leaving it: ProjectItem::sanitize() settles it exactly, by comparing the canvas fingerprint
 * recorded per input at render time.
 *
 * \tparam Profiles Vector of profiles exposing \c id and \c name.
 * \tparam MakeId   Callable returning a fresh unique id for that vector.
 */
template <typename Profiles, typename MakeId>
void deduplicateIds(Profiles&                                              profiles,
                    const MakeId&                                          makeFreshId,
                    std::vector<WorkspaceRepairReport::ReassignedProfile>&  out)
{
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

    Models::Workspace workspace;
    try {
        workspace = j.get<Models::Workspace>();
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(
            "WorkspaceSerializer::load() — schema error in '" +
            filePath + "': " + e.what());
    }

    if (fileVersion < k_currentVersion) {
        migrate(workspace, fileVersion);
    }

    // Identifier repair (runs for ANY version) — see the helpers above.
    //
    // Mint first, then deduplicate.  A file can arrive needing both: profiles saved
    // without an id at all (pre-id workspaces, or a GUI that wiped the id on edit) and
    // profiles that share one (ids used to be a millisecond timestamp, so several minted
    // in the same loop came out identical).
    mintMissingProfileIds(workspace);

    deduplicateIds(
        workspace.canvasProfiles,
        [](const auto& existing) { return makeUniqueCanvasProfileId(existing); },
        report.canvasProfiles);

    deduplicateIds(
        workspace.outputProfiles,
        [](const auto& existing) { return makeUniqueOutputProfileId(existing); },
        report.outputProfiles);

    // Presets last: adoption and forking both hand out ids, so they must run on a list that
    // is already free of duplicates, and the presence pass must see the final set.
    enforceOutputProfilePresets(workspace);

    // Rebuild runtime lookup tables for every project.
    // These tables are not serialised so they must always be reconstructed
    // after loading from JSON.
    for (auto& pi : workspace.projectItems)
        pi.rebuildLookupTables();

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
