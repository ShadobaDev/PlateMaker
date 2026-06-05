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

// --- CanvasProfile ---
void to_json(nlohmann::json& j, const CanvasProfile& v) {
    j = nlohmann::json{
        {"name",             v.name},
        {"canvasSize",       v.canvasSize},
        {"margins",          v.margins},
        {"visualColour",     v.visualColour},
        {"backgroundColour", v.backgroundColour}
    };
}
void from_json(const nlohmann::json& j, CanvasProfile& v) {
    j.at("name").get_to(v.name);
    j.at("canvasSize").get_to(v.canvasSize);
    j.at("margins").get_to(v.margins);
    // Use contains() for colour fields so older workspace files without these
    // keys load gracefully with the struct's default values.
    if (j.contains("visualColour"))
        j.at("visualColour").get_to(v.visualColour);
    if (j.contains("backgroundColour"))
        j.at("backgroundColour").get_to(v.backgroundColour);
}

// --- OutputProfile ---
void to_json(nlohmann::json& j, const OutputProfile& v) {
    j = nlohmann::json{
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
        {"name",            v.name},
        {"uuid",            v.uuid},
        {"inputDirectory",  v.inputDirectory},
        {"inputFiles",      v.getInputImages()},
        {"outputFiles",     v.getOutputImages()},
        {"outputDirectory", v.getOutputDirectory()}
    };
}
void from_json(const nlohmann::json& j, ProjectItem& v) {
    if (j.contains("name"))           j.at("name").get_to(v.name);
    if (j.contains("uuid"))           j.at("uuid").get_to(v.uuid);
    if (j.contains("inputDirectory")) j.at("inputDirectory").get_to(v.inputDirectory);
    j.at("inputFiles").get_to(v.getInputImages());
    j.at("outputFiles").get_to(v.getOutputImages());
    j.at("outputDirectory").get_to(v.getOutputDirectory());
}

// --- Workspace ---
void to_json(nlohmann::json& j, const Workspace& v) {
    j = nlohmann::json{
        {"version",                 v.version},
        {"canvasProfiles",          v.canvasProfiles},
        {"outputProfiles",          v.outputProfiles},
        {"activeCanvasProfileName", v.activeCanvasProfileName},
        {"activeOutputProfileName", v.activeOutputProfileName},
        {"projectItems",            v.projectItems},
        {"outputDirectory",         v.outputDirectory},
        {"stripDirty",              v.stripDirty}
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
    j.at("activeCanvasProfileName").get_to(v.activeCanvasProfileName);
    j.at("activeOutputProfileName").get_to(v.activeOutputProfileName);
    if (j.contains("projectItems")) j.at("projectItems").get_to(v.projectItems);
    if (j.contains("outputDirectory")) j.at("outputDirectory").get_to(v.outputDirectory);
    if (j.contains("stripDirty")) j.at("stripDirty").get_to(v.stripDirty);
}

} // namespace Platemaker::Models

// ---------------------------------------------------------------------------
// WorkspaceSerializer implementation
// ---------------------------------------------------------------------------
namespace Platemaker::Infrastructure {

namespace {
    /// Current on-disk schema version.  Increment on every breaking schema change.
    constexpr int k_currentVersion = 1;
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

    return workspace;
}

// ---------------------------------------------------------------------------
// save (atomic write: temp file → rename)
// ---------------------------------------------------------------------------

void WorkspaceSerializer::save(
    const Models::Workspace& workspace,
    const std::string&       filePath) const
{
    namespace fs = std::filesystem;

    const nlohmann::json j  = workspace;
    const std::string  text = j.dump(4); // pretty-print, 4-space indent

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
    Models::Workspace& /*workspace*/,
    int                fromVersion) const
{
    // Migration chain.  Add a new block for each future schema version bump:
    //
    //   if (fromVersion < 2) {
    //       // apply changes to bring a v1 workspace to v2 in-place
    //   }
    //
    // For now version 1 is the only version, so nothing to do.
    (void)fromVersion;
}

} // namespace Platemaker::Infrastructure
