/**
 * \file lib/src/infrastructure/profile_bundle_serializer/profile_bundle_serializer.cpp
 * \brief ProfileBundleSerializer implementation — JSON round-trip for a portable profile set.
 *
 * Reuses the shared Models JSON codec (infrastructure/model_json) so a bundle and a workspace agree
 * on how a CanvasProfile / OutputProfile is written. The two bundle invariants (no templates, no
 * presets) are applied here on write, so a file on disk always satisfies them.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-27
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>

#include "infrastructure/model_json/model_json.hpp"   // the shared Models JSON codec
#include "infrastructure/file/atomic_file.hpp"            // writeTextFileAtomically()

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/models/output_profile.hpp>        // outputPresetDefById

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <platemaker/models/output_presets.hpp>

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
    // templates, and any preset-id output profile is dropped (presets are code-defined).
    std::vector<Models::CanvasProfile> canvas = canvasProfiles;
    for (auto& cp : canvas)
        cp.templates.clear();

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
    const std::string text = serialize(canvasProfiles, outputProfiles);

    // Whole or not at all, as WorkspaceSerializer::save(): a crash mid-write must not corrupt an
    // existing bundle (a GUI-managed library is the user's whole saved set).
    writeTextFileAtomically(utf8ToPath(filePath), text, "ProfileBundleSerializer::save()");
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
