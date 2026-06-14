/**
 * \file lib/src/core/canvas_profile_matcher/canvas_profile_matcher.cpp
 * \brief CanvasProfileMatcher implementation.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-14
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include "platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp"

#include <algorithm>

namespace Platemaker::Core {

CanvasProfileMatcher::CanvasProfileMatcher(
    const std::vector<Models::CanvasProfile>& allWorkspaceProfiles,
    const std::vector<std::string>&           projectProfileIds)
{
    if (projectProfileIds.empty()) {
        // No per-project filtering: every workspace profile is treated as project-linked.
        // resolve() will return Matched (not FoundInWorkspaceOnly) for any hit.
        for (const auto& cp : allWorkspaceProfiles)
            m_projectProfiles.push_back(&cp);
        return;
    }

    // subA: project-linked profiles in priority order (order of projectProfileIds).
    for (const auto& id : projectProfileIds) {
        for (const auto& cp : allWorkspaceProfiles) {
            if (cp.id == id) {
                m_projectProfiles.push_back(&cp);
                break;
            }
        }
    }

    // subB: workspace profiles not in the project list (fallback pool).
    for (const auto& cp : allWorkspaceProfiles) {
        const bool inProject = std::any_of(
            projectProfileIds.begin(), projectProfileIds.end(),
            [&](const std::string& id) { return cp.id == id; });
        if (!inProject)
            m_workspaceOnlyProfiles.push_back(&cp);
    }
}

ProfileMatchResult CanvasProfileMatcher::resolve(int w, int h) const
{
    // Step 1: search project-linked profiles (subA).
    // Conflict guard guarantees at most one match here, so the first hit is final.
    for (const auto* cp : m_projectProfiles) {
        if (cp->canvasSize.width == w && cp->canvasSize.height == h)
            return {ProfileMatchResult::Status::Matched, cp, {}};
    }

    // Step 2: search workspace-only profiles (subB).
    // Collect all matches so the caller can present them to the user.
    std::vector<const Models::CanvasProfile*> candidates;
    for (const auto* cp : m_workspaceOnlyProfiles) {
        if (cp->canvasSize.width == w && cp->canvasSize.height == h)
            candidates.push_back(cp);
    }

    if (!candidates.empty())
        return {ProfileMatchResult::Status::FoundInWorkspaceOnly, nullptr, std::move(candidates)};

    return {ProfileMatchResult::Status::NotFoundAnywhere, nullptr, {}};
}

} // namespace Platemaker::Core
