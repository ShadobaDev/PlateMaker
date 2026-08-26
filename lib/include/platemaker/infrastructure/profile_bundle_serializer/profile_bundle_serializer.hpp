/**
 * \file lib/include/platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp
 * \brief ProfileBundleSerializer — reads and writes a portable set of canvas + output profiles
 *        as a standalone \c .platemaker.profiles.json file, independent of any workspace.
 *
 * A *profile bundle* is the transfer unit for moving profiles between workspaces or sharing them.
 * It is deliberately NOT a workspace: it carries only the two profile palettes, so it stays small
 * and has no project/output-directory state. The library treats a bundle purely as data; where a
 * bundle file lives (a user-picked path, or a GUI-managed library in the OS app-data directory) is
 * the consumer's concern — the library never chooses a location.
 *
 * Two invariants of a bundle, enforced on write:
 *  - \b No \c CanvasTemplateInfo. A template's path is relative to a specific workspace directory and
 *    is meaningless outside it, so it is stripped — a bundle profile always reads as "no template".
 *  - \b No presets. Output presets are code-defined and resolved from the catalogue, never persisted,
 *    so any preset-id profile handed in is dropped (mirrors WorkspaceEditor::replaceOutputProfiles).
 *
 * Importing a bundle into a workspace is a separate step, owned by \c WorkspaceEditor::importProfiles
 * (fresh ids, template cleared, presets dropped) so the same transfer rules back both the GUI and CLI.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_PROFILE_BUNDLE_SERIALIZER_HPP
#define PLATEMAKER_INFRASTRUCTURE_PROFILE_BUNDLE_SERIALIZER_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Infrastructure {

/**
 * \struct ProfileBundle
 * \brief The two profile palettes carried by a \c .platemaker.profiles.json file.
 *
 * The result of loading a bundle, and the natural argument set for
 * \c WorkspaceEditor::importProfiles(). Nothing else is stored in a bundle.
 */
struct ProfileBundle {
    std::vector<Models::CanvasProfile> canvasProfiles; //!< Canvas profiles (templateInfo already stripped).
    std::vector<Models::OutputProfile> outputProfiles; //!< Output profiles (presets already dropped).
};

/**
 * \class ProfileBundleSerializer
 * \brief Serialises and deserialises a \ref ProfileBundle to and from a UTF-8 JSON file.
 *
 * The on-disk format is a single JSON object with a top-level \c "version" integer, independent of
 * the workspace schema version, plus \c "canvasProfiles" and \c "outputProfiles" arrays. Writes are
 * atomic (temp file then rename), matching WorkspaceSerializer. Stateless and thread-safe for reads.
 */
class PLATEMAKER_EXPORT ProfileBundleSerializer {
public:
    ProfileBundleSerializer() = default;

    /**
     * \brief Serialises the given palettes to the canonical bundle JSON string.
     *
     * Strips \c CanvasTemplateInfo from every canvas profile and drops any output profile carrying a
     * baked-in preset id, so the returned text already satisfies the bundle invariants.
     *
     * \return The pretty-printed JSON representation.
     */
    [[nodiscard]] std::string serialize(const std::vector<Models::CanvasProfile>& canvasProfiles,
                                        const std::vector<Models::OutputProfile>& outputProfiles) const;

    /**
     * \brief Parses a bundle from its JSON string form.
     *
     * \param text A string previously produced by \c serialize() (or read from a bundle file).
     * \return The parsed palettes.
     * \throws std::runtime_error if the text is not valid JSON, is missing the \c version field, or
     *         carries a version this build does not understand.
     */
    [[nodiscard]] ProfileBundle deserialize(const std::string& text) const;

    /**
     * \brief Saves the given palettes to a \c .platemaker.profiles.json file (atomic write).
     *
     * \param canvasProfiles Canvas palette to write (templateInfo stripped).
     * \param outputProfiles Output palette to write (presets dropped).
     * \param filePath       Absolute path of the destination file; created or overwritten.
     * \throws std::runtime_error if the file cannot be written.
     */
    void save(const std::vector<Models::CanvasProfile>& canvasProfiles,
              const std::vector<Models::OutputProfile>& outputProfiles,
              const std::string&                        filePath) const;

    /**
     * \brief Loads a bundle from a \c .platemaker.profiles.json file.
     *
     * \param filePath Absolute path to the bundle file.
     * \return The parsed palettes.
     * \throws std::runtime_error if the file cannot be opened, is not valid JSON, or has an
     *         unsupported version.
     */
    [[nodiscard]] ProfileBundle load(const std::string& filePath) const;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_PROFILE_BUNDLE_SERIALIZER_HPP
