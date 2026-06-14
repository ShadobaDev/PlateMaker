/**
 * \file lib/include/platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp
 * \brief CanvasProfileMatcher — resolves a canvas profile for a given image size.
 *
 * Constructed once per processing run with the workspace profile palette and the
 * project's ordered profile-id list.  \c resolve() is then called once per input
 * image in O(N_project_profiles) without further allocation.
 *
 * See SPECIFICATION.md §7.5 for the full algorithm and error semantics.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-14
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_CANVAS_PROFILE_MATCHER_HPP
#define PLATEMAKER_CORE_CANVAS_PROFILE_MATCHER_HPP

#include "platemaker/platemaker_export.h"
#include "platemaker/models/canvas_profile.hpp"

#include <string>
#include <vector>

namespace Platemaker::Core {

/**
 * \brief Result of a single \c CanvasProfileMatcher::resolve() call.
 */
struct ProfileMatchResult {
    enum class Status {
        Matched,              ///< Profile found in project-linked list — use \c profile.
        NotFoundAnywhere,     ///< No W×H match in project list or workspace.
        FoundInWorkspaceOnly, ///< Found in workspace palette but not linked to this project.
    };

    Status status = Status::NotFoundAnywhere;

    /// Non-null only when \c status == Matched.
    const Models::CanvasProfile* profile = nullptr;

    /// Non-empty only when \c status == FoundInWorkspaceOnly.
    /// Contains all workspace profiles whose dimensions match W×H.
    std::vector<const Models::CanvasProfile*> workspaceCandidates;
};

/**
 * \class CanvasProfileMatcher
 * \brief Partition-based canvas profile resolver.
 *
 * The constructor splits the workspace profile palette into two ordered subsets:
 *   - \b subA: profiles whose \c id is listed in \p projectProfileIds (priority order).
 *   - \b subB: remaining workspace profiles (fallback pool).
 *
 * When \p projectProfileIds is empty — i.e. the project has no per-project profile
 * filtering yet — all workspace profiles are placed in subA.  This preserves
 * pre-UUID behaviour: every workspace profile is accepted and a match returns
 * \c Status::Matched instead of \c FoundInWorkspaceOnly.
 *
 * The conflict guard (SPECIFICATION.md §7.5.2) guarantees that subA contains at
 * most one profile per W×H pair, so the first hit in subA is always final.
 */
class PLATEMAKER_EXPORT CanvasProfileMatcher {
public:
    /**
     * \brief Partitions the workspace profiles into subA and subB.
     *
     * \param allWorkspaceProfiles  The complete palette from \c Workspace::canvasProfiles.
     *                              Must outlive this object.
     * \param projectProfileIds     Ordered list of \c CanvasProfile::id values linked to
     *                              the project (\c ProjectItem::canvasProfileIds).
     *                              Pass \c {} when the project has no per-project list.
     */
    CanvasProfileMatcher(
        const std::vector<Models::CanvasProfile>& allWorkspaceProfiles,
        const std::vector<std::string>&           projectProfileIds = {});

    /**
     * \brief Resolves the canvas profile for one input image of size \p w × \p h.
     *
     * Search order (see SPECIFICATION.md §7.5.1):
     *   1. subA (project-linked) — O(N_project_profiles); first match is final.
     *   2. subB (workspace-only) — O(N_workspace − N_project_profiles); all
     *      matching profiles are returned in \c workspaceCandidates.
     *
     * \param w  Image width in pixels.
     * \param h  Image height in pixels.
     * \return   \c ProfileMatchResult describing the outcome.
     */
    [[nodiscard]] ProfileMatchResult resolve(int w, int h) const;

private:
    std::vector<const Models::CanvasProfile*> m_projectProfiles;       ///< subA
    std::vector<const Models::CanvasProfile*> m_workspaceOnlyProfiles; ///< subB
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_CANVAS_PROFILE_MATCHER_HPP
