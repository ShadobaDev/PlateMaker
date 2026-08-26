/**
 * \file lib/src/infrastructure/profile_bundle_serializer/profile_bundle_serializer.cpp
 * \brief ProfileBundleSerializer implementation — JSON round-trip for a portable profile set.
 *
 * Reuses the shared Models JSON codec (infrastructure/model_json) so a bundle and a workspace agree
 * on how a CanvasProfile / OutputProfile is written. The two bundle invariants (no templateInfo, no
 * presets) are applied here on write, so a file on disk always satisfies them.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>

#include "infrastructure/model_json/model_json.hpp"   // the shared Models JSON codec

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/models/output_profile.hpp>        // outputPresetDefById

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Platemaker::Infrastructure {

namespace {
    //! Current on-disk bundle version. Independent of the workspace schema version.
    constexpr int k_bundleVersion = 1;
} // anonymous namespace

// ---------------------------------------------------------------------------
// serialize / deserialize (string form)
// ---------------------------------------------------------------------------

std::string ProfileBundleSerializer::serialize(
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<Models::OutputProfile>& outputProfiles) const
{
    // Enforce the bundle invariants on write: a canvas profile loses its workspace-relative
    // templateInfo, and any preset-id output profile is dropped (presets are code-defined).
    std::vector<Models::CanvasProfile> canvas = canvasProfiles;
    for (auto& cp : canvas)
        cp.templateInfo = {};

    std::vector<Models::OutputProfile> output;
    output.reserve(outputProfiles.size());
    for (const auto& op : outputProfiles)
        if (!Models::outputPresetDefById(op.id))
            output.push_back(op);

    const nlohmann::json j{
        {"version",        k_bundleVersion},
        {"canvasProfiles", canvas},
        {"outputProfiles", output}
    };
    return j.dump(4); // pretty-print, 4-space indent (matches WorkspaceSerializer)
}

ProfileBundle ProfileBundleSerializer::deserialize(const std::string& text) const
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("ProfileBundleSerializer::deserialize() — JSON parse error: ") + e.what());
    }

    if (!j.contains("version") || !j.at("version").is_number_integer()) {
        throw std::runtime_error(
            "ProfileBundleSerializer::deserialize() — missing or invalid 'version' field");
    }
    const int version = j.at("version").get<int>();
    if (version > k_bundleVersion) {
        throw std::runtime_error(
            "ProfileBundleSerializer::deserialize() — unsupported bundle version " +
            std::to_string(version) + " (this build understands up to " +
            std::to_string(k_bundleVersion) + ")");
    }

    ProfileBundle bundle;
    try {
        // value() tolerates an absent array (an all-canvas or all-output bundle is valid).
        bundle.canvasProfiles = j.value("canvasProfiles", std::vector<Models::CanvasProfile>{});
        bundle.outputProfiles = j.value("outputProfiles", std::vector<Models::OutputProfile>{});
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(
            std::string("ProfileBundleSerializer::deserialize() — schema error: ") + e.what());
    }
    return bundle;
}

// ---------------------------------------------------------------------------
// save (atomic write: temp file → rename) / load
// ---------------------------------------------------------------------------

void ProfileBundleSerializer::save(
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<Models::OutputProfile>& outputProfiles,
    const std::string&                        filePath) const
{
    namespace fs = std::filesystem;

    const std::string text = serialize(canvasProfiles, outputProfiles);

    // Sibling temp file then rename, mirroring WorkspaceSerializer::save() — a mid-write crash must
    // not corrupt an existing bundle (a GUI-managed library is the user's whole saved set). Paths go
    // through utf8ToPath()/pathToUtf8() so non-ASCII locations round-trip on Windows.
    const fs::path finalPath = utf8ToPath(filePath);
    const fs::path tmpPath   =
        finalPath.parent_path() / utf8ToPath(pathToUtf8(finalPath.filename()) + ".tmp");

    {
        std::ofstream tmp(tmpPath, std::ios::out | std::ios::trunc);
        if (!tmp.is_open()) {
            throw std::runtime_error(
                "ProfileBundleSerializer::save() — cannot open temp file '" +
                pathToUtf8(tmpPath) + "' for writing");
        }
        tmp << text;
        if (!tmp.good()) {
            throw std::runtime_error(
                "ProfileBundleSerializer::save() — write error for '" +
                pathToUtf8(tmpPath) + "'");
        }
    } // tmp closed and flushed here

    std::error_code ec;
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        // Fallback for cross-device rename (uncommon but possible on Windows).
        fs::copy_file(tmpPath, finalPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::remove(tmpPath, ec);
        if (ec) {
            throw std::runtime_error(
                "ProfileBundleSerializer::save() — cannot move temp file to '" +
                filePath + "': " + ec.message());
        }
    }
}

ProfileBundle ProfileBundleSerializer::load(const std::string& filePath) const
{
    std::ifstream file(utf8ToPath(filePath));
    if (!file.is_open()) {
        throw std::runtime_error(
            "ProfileBundleSerializer::load() — cannot open '" + filePath + "'");
    }

    std::string text((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    try {
        return deserialize(text);
    } catch (const std::runtime_error& e) {
        // Re-tag with the path so a bad file is identifiable.
        throw std::runtime_error(
            "ProfileBundleSerializer::load() — in '" + filePath + "': " + e.what());
    }
}

} // namespace Platemaker::Infrastructure
