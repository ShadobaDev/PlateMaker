/**
 * \file lib/include/platemaker/models/workspace.hpp
 * \brief Workspace data model — the root document type persisted as a .platemaker.json file.
 *
 * Also contains ProcessedFileRecord, the per-file incremental-processing cache entry.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_WORKSPACE_HPP
#define PLATEMAKER_MODELS_WORKSPACE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "platemaker/platemaker_export.h"
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Models {

/**
 * \class Workspace
 * \brief The root document that is serialised to and deserialised from
 *        \c <project-name>.platemaker.json.
 *
 * A workspace holds all project settings, the ordered page list, profile
 * definitions, and the incremental-processing cache.  It is the single source
 * of truth passed between all components of libplatemaker.
 *
 * \note The \c version field drives the migration chain in WorkspaceSerializer.
 *       Increment it whenever the serialised schema changes in a breaking way.
 *
 * \warning \c stripDirty must be set to \c true whenever the page order changes,
 *          a page is added or removed, or the active OutputProfile changes.  When
 *          \c true the entire strip is reprocessed regardless of individual file
 *          hashes.
 */
class Workspace {
public:
    Workspace() = default;
    ~Workspace() = default;

    // Non-copyable: std::vector<ProjectItem> cannot be copied (ProjectItem is move-only).
    Workspace(const Workspace&)            = delete;
    Workspace& operator=(const Workspace&) = delete;

    Workspace(Workspace&&)            = default;
    Workspace& operator=(Workspace&&) = default;

    /**
     * \brief Schema version number.
     *
     * Start at 1; increment on each breaking schema change.  WorkspaceSerializer
     * uses this to run the appropriate migration chain when loading older files.
     */
    int version = 1;

    std::vector<CanvasProfile> canvasProfiles;  //!< All defined canvas profiles.
    std::vector<OutputProfile> outputProfiles;  //!< All defined output profiles.

    std::string outputDirectory; //!< Absolute path to the directory where output slices are written.

    /**
     * \brief Incremental processing cache — one entry per source file that has been processed.
     *
     * On each run the pipeline compares current file hashes against these records
     * to determine which outputs are still valid.
     */
    std::vector<ProjectItem> projectItems; //!< List of all projects (input and output files) that have been processed at least once.

    /**
     * \brief Indicates that the entire strip must be reprocessed on the next run.
     *
     * Set to \c true automatically by the workspace management layer whenever the
     * strip composition changes (page added, removed, reordered, or OutputProfile
     * modified).  Reset to \c false after a successful full reprocess.
     */
    bool stripDirty = false;
};

/**
 * \brief Resolves an output-profile id against the workspace's user profiles and the baked-in
 *        preset catalogue.
 *
 * Presets take precedence — a preset id is reserved and must not be shadowed by a user profile — and
 * the scan is linear: the catalogue holds a handful of entries and a workspace a few profiles, so no
 * index is warranted. This is the one place both the CLI and the GUI should resolve through, so the
 * union of user profiles and presets is defined once. Whether the result *is* a preset is a separate
 * question — irrelevant to rendering, and answered without a copy by
 * \c outputPresetDefById(id) where a consumer needs it (to block editing or removing a preset).
 *
 * \return The resolved profile, or \c std::nullopt if \p id names neither. One copy: the returned
 *         optional owns its OutputProfile (C++20 optional cannot hold a reference).
 */
[[nodiscard]] inline std::optional<OutputProfile>
resolveOutputProfile(const Workspace& ws, std::string_view id)
{
    if (auto preset = outputProfilePresetById(id))
        return preset;
    for (const auto& op : ws.outputProfiles)
        if (op.id == id)
            return op;
    return std::nullopt;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_WORKSPACE_HPP
