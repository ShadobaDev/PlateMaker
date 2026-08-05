/**
 * \file lib/src/infrastructure/model_json/model_json.hpp
 * \brief nlohmann/json (de)serialisation for every Platemaker::Models type — the shared codec.
 *
 * These \c to_json / \c from_json overloads live in the \c Platemaker::Models namespace so
 * nlohmann/json finds them by argument-dependent lookup.  They are declared here (and defined in
 * model_json.cpp) so more than one component can use the JSON format without depending on each
 * other: \c WorkspaceSerializer uses them for file I/O, and \c ProjectEditor / \c WorkspaceEditor
 * use them for the partial snapshot/restore that undo/redo is built on.  Keeping the codec here
 * (an internal src-only header, never installed) also keeps the public model headers free of any
 * third-party dependency.
 *
 * Ordering note: because everything is declared up front, definition order in model_json.cpp is
 * irrelevant — a serialiser may freely call another.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_MODEL_JSON_HPP
#define PLATEMAKER_INFRASTRUCTURE_MODEL_JSON_HPP

#include <nlohmann/json.hpp>

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>

namespace Platemaker::Models {

// --- Enumerations (string-based for human-readable JSON) ---
// NLOHMANN_JSON_SERIALIZE_ENUM emits inline functions, so they are safe to define in a header
// included by multiple translation units.

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
    {FileStatus::Done,           "Done"},
    {FileStatus::Skipped,        "Skipped"},
    {FileStatus::Error,          "Error"}
})

// --- Struct codecs (declarations; defined in model_json.cpp) ---

void to_json(nlohmann::json& j, const RGBA& v);
void from_json(const nlohmann::json& j, RGBA& v);

void to_json(nlohmann::json& j, const Size& v);
void from_json(const nlohmann::json& j, Size& v);

void to_json(nlohmann::json& j, const Margins& v);
void from_json(const nlohmann::json& j, Margins& v);

void to_json(nlohmann::json& j, const JpegOptions& v);
void from_json(const nlohmann::json& j, JpegOptions& v);

void to_json(nlohmann::json& j, const PngOptions& v);
void from_json(const nlohmann::json& j, PngOptions& v);

void to_json(nlohmann::json& j, const WebpOptions& v);
void from_json(const nlohmann::json& j, WebpOptions& v);

void to_json(nlohmann::json& j, const CanvasTemplateInfo& v);
void from_json(const nlohmann::json& j, CanvasTemplateInfo& v);

void to_json(nlohmann::json& j, const CanvasProfile& v);
void from_json(const nlohmann::json& j, CanvasProfile& v);

void to_json(nlohmann::json& j, const OutputProfile& v);
void from_json(const nlohmann::json& j, OutputProfile& v);

void to_json(nlohmann::json& j, const InputFile& v);
void from_json(const nlohmann::json& j, InputFile& v);

void to_json(nlohmann::json& j, const SourceSegment& v);
void from_json(const nlohmann::json& j, SourceSegment& v);

void to_json(nlohmann::json& j, const OutputFile& v);
void from_json(const nlohmann::json& j, OutputFile& v);

void to_json(nlohmann::json& j, const ProjectItem& v);
void from_json(const nlohmann::json& j, ProjectItem& v);

void to_json(nlohmann::json& j, const Workspace& v);
void from_json(const nlohmann::json& j, Workspace& v);

} // namespace Platemaker::Models

#endif // PLATEMAKER_INFRASTRUCTURE_MODEL_JSON_HPP
