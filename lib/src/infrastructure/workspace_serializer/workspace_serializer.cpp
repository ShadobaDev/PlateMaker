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

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

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
    if (j.contains("id"))
        j.at("id").get_to(v.id);
    else
        v.id = "cp-" + j.at("name").get<std::string>(); // back-compat: derive from name
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
        {"contributesTo", v.contributesTo}
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
} // anonymous namespace

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

Models::Workspace WorkspaceSerializer::load(const std::string& filePath) const
{
    std::ifstream file(filePath);
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
    const fs::path finalPath{filePath};
    const fs::path tmpPath =
        finalPath.parent_path() / (finalPath.filename().string() + ".tmp");

    {
        std::ofstream tmp(tmpPath, std::ios::out | std::ios::trunc);
        if (!tmp.is_open()) {
            throw std::runtime_error(
                "WorkspaceSerializer::save() — cannot open temp file '" +
                tmpPath.string() + "' for writing");
        }
        tmp << text;
        if (!tmp.good()) {
            throw std::runtime_error(
                "WorkspaceSerializer::save() — write error for '" +
                tmpPath.string() + "'");
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
        // Assign stable IDs to OutputProfiles that were deserialized without one.
        for (auto& op : workspace.outputProfiles) {
            if (op.id.empty())
                op.id = "op-" + op.name;
        }
    }
}

} // namespace Platemaker::Infrastructure
