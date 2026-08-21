/**
 * \file lib/src/infrastructure/model_json/model_json.cpp
 * \brief Definitions of the Platemaker::Models JSON codec declared in model_json.hpp.
 *
 * Moved out of workspace_serializer.cpp so the same codec can be shared by the serializer (file
 * I/O) and by ProjectEditor / WorkspaceEditor (partial snapshot/restore for undo/redo).
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include "infrastructure/model_json/model_json.hpp"

#include <stdexcept>
#include <vector>

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
        {"uid", v.uid},
        {"filePath", v.filePath},
        {"sha256", v.sha256},
        {"order", v.order},
        {"thumbnailPath", v.thumbnailPath},
        {"status", v.status},
        {"lastProcessed", v.lastProcessed},
        {"contributesTo", v.contributesTo},
        {"canvasProfileId", v.canvasProfileId},
        {"canvasFingerprint", v.canvasFingerprint},
        {"width", v.width},
        {"height", v.height}
    };
}
void from_json(const nlohmann::json& j, InputFile& v) {
    // uid: absent in workspaces written under the old "uuid" key (no longer read) — left empty and
    // minted by ProjectItem::ensureUniqueFileUids() at load time.
    if (j.contains("uid")) j.at("uid").get_to(v.uid);
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
    // additive — the recorded display W×H (0 = unknown). Absent in workspaces written before
    // per-input dimensions were tracked; detectCanvasConfigChange() then falls back to the coarse
    // list comparison for that page until the next render records its size.
    if (j.contains("width"))  j.at("width").get_to(v.width);
    if (j.contains("height")) j.at("height").get_to(v.height);
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
        {"uid", v.uid},
        {"fileName", v.fileName},
        {"sha256", v.sha256},
        {"sourceMap", v.sourceMap},
        {"status", v.status}
    };
}
void from_json(const nlohmann::json& j, OutputFile& v) {
    if (j.contains("uid")) j.at("uid").get_to(v.uid); // minted at load if absent (old "uuid" ignored)
    j.at("fileName").get_to(v.fileName);
    j.at("sha256").get_to(v.sha256);
    j.at("sourceMap").get_to(v.sourceMap);
    j.at("status").get_to(v.status);
}

// --- ProjectItem ---
void to_json(nlohmann::json& j, const ProjectItem& v) {
    j = nlohmann::json{
        {"name",             v.name},
        {"uid",              v.uid},
        {"inputDirectory",   v.inputDirectory},
        {"canvasProfileIds", v.canvasProfileIds()},
        {"outputProfileId",  v.outputProfileId()},
        {"outputSignature",  v.outputSignature},
        {"canvasProfileIdsAtRender", v.canvasProfileIdsAtRender},
        {"inputOrderAtRender", v.inputOrderAtRender},
        {"inputFiles",       v.getInputImages()},
        {"outputFiles",      v.getOutputImages()},
        {"outputDirectory",  v.getOutputDirectory()}
    };
}
void from_json(const nlohmann::json& j, ProjectItem& v) {
    if (j.contains("name"))             j.at("name").get_to(v.name);
    if (j.contains("uid"))              j.at("uid").get_to(v.uid); // minted at load if absent (old "uuid" ignored)
    if (j.contains("inputDirectory"))   j.at("inputDirectory").get_to(v.inputDirectory);
    // canvasProfileIds / outputProfileId are intentionally NOT read here: they are private and only
    // WorkspaceEditor / WorkspaceSerializer / ProjectEditor may write them. load() installs them per
    // project, and ProjectEditor::restore() reads them from the same JSON via the friend path.
    if (j.contains("outputSignature"))  j.at("outputSignature").get_to(v.outputSignature);
    if (j.contains("canvasProfileIdsAtRender"))
        j.at("canvasProfileIdsAtRender").get_to(v.canvasProfileIdsAtRender);
    if (j.contains("inputOrderAtRender"))
        j.at("inputOrderAtRender").get_to(v.inputOrderAtRender);
    j.at("inputFiles").get_to(v.getInputImages());
    j.at("outputFiles").get_to(v.getOutputImages());
    j.at("outputDirectory").get_to(v.getOutputDirectory());
}

// --- Workspace ---
void to_json(nlohmann::json& j, const Workspace& v) {
    // Presets are baked-in and must never be persisted; a preset-id profile in outputProfiles is a
    // consumer error (the model is that only user profiles live there). Filtering it out here makes
    // the serializer the lib's own guarantee that a preset cannot be written into a workspace,
    // independent of whether the CLI/GUI played by the rules.
    nlohmann::json outputProfiles = nlohmann::json::array();
    for (const auto& op : v.outputProfiles())
        if (!outputProfilePresetById(op.id))
            outputProfiles.push_back(op);

    j = nlohmann::json{
        {"version",         v.version},
        {"canvasProfiles",  v.canvasProfiles()},
        {"outputProfiles",  outputProfiles},
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
    // Profile palettes are intentionally NOT read here: Workspace's vectors are private and only
    // WorkspaceEditor may write them.  load() parses the "canvasProfiles"/"outputProfiles" arrays
    // separately and hands them to WorkspaceEditor::installLoaded(), which enforces the invariants
    // (unique ids, no persisted presets) a loaded file is subject to.  Anyone calling
    // j.get<Workspace>() directly therefore gets a workspace with empty palettes by design.
    // activeCanvasProfileName / activeOutputProfileName removed in schema v2 — silently ignored.
    if (j.contains("projectItems"))    j.at("projectItems").get_to(v.projectItems);
    if (j.contains("outputDirectory")) j.at("outputDirectory").get_to(v.outputDirectory);
    if (j.contains("stripDirty"))      j.at("stripDirty").get_to(v.stripDirty);
}

} // namespace Platemaker::Models
