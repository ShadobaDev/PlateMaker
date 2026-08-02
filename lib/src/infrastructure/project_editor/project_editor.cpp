/**
 * \file lib/src/infrastructure/project_editor/project_editor.cpp
 * \brief ProjectEditor implementation — input ordering for a single project.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-29
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/project_editor/project_editor.hpp>

#include "infrastructure/model_json/model_json.hpp"   // ProjectItem JSON codec

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Platemaker::Infrastructure {

bool ProjectEditor::setInputOrder(const std::vector<std::string>& orderedUids)
{
    auto& inputs = m_project.getInputImages();

    // Must be a permutation of the current input uids: same count, distinct, and each one exists.
    if (orderedUids.size() != inputs.size())
        return false;

    std::unordered_map<std::string, Models::InputFile*> byUid;
    byUid.reserve(inputs.size());
    for (auto& f : inputs)
        byUid.emplace(f.uid, &f);

    const std::unordered_set<std::string> distinct(orderedUids.begin(), orderedUids.end());
    if (distinct.size() != inputs.size())
        return false; // a duplicate (and therefore a missing) uid

    for (const auto& uid : orderedUids)
        if (byUid.find(uid) == byUid.end())
            return false; // an unknown uid

    // Valid permutation → assign order = position. The vector itself is left untouched.
    for (int i = 0; i < static_cast<int>(orderedUids.size()); ++i)
        byUid[orderedUids[static_cast<std::size_t>(i)]]->order = i;

    return true;
}

bool ProjectEditor::moveInput(const std::string& uid, int delta)
{
    const auto& inputs = m_project.getInputImages();
    if (inputs.size() < 2)
        return false;

    // Current strip order (by the order field), as uids.
    std::vector<const Models::InputFile*> ptrs;
    ptrs.reserve(inputs.size());
    for (const auto& f : inputs)
        ptrs.push_back(&f);
    std::stable_sort(ptrs.begin(), ptrs.end(),
                     [](const Models::InputFile* a, const Models::InputFile* b) {
                         return a->order < b->order;
                     });

    std::vector<std::string> ordered;
    ordered.reserve(ptrs.size());
    for (const auto* p : ptrs)
        ordered.push_back(p->uid);

    int idx = -1;
    for (int i = 0; i < static_cast<int>(ordered.size()); ++i)
        if (ordered[static_cast<std::size_t>(i)] == uid) { idx = i; break; }

    const int target = idx + delta;
    if (idx < 0 || target < 0 || target >= static_cast<int>(ordered.size()))
        return false; // unknown uid or already at the edge

    std::swap(ordered[static_cast<std::size_t>(idx)], ordered[static_cast<std::size_t>(target)]);
    return setInputOrder(ordered);
}

std::string ProjectEditor::snapshot() const
{
    // to_json(ProjectItem) reads the profile links via the public accessors, so a plain conversion
    // captures the full project. Compact dump keeps the in-memory undo snapshot small.
    const nlohmann::json j = m_project;
    return j.dump();
}

void ProjectEditor::restore(const std::string& snapshot)
{
    const nlohmann::json j = nlohmann::json::parse(snapshot);

    // name is workspace-owned (renamed via WorkspaceEditor on the workspace timeline); a project-scope
    // restore must keep the current name rather than resurrect the one stored in the snapshot.
    std::string keepName = m_project.name;

    // from_json fills the public fields + inputs/outputs but deliberately skips the private link
    // fields; set those from the same JSON through the friend path (a previously valid state).
    Models::ProjectItem restored = j.get<Models::ProjectItem>();
    restored.name              = std::move(keepName);
    restored.m_canvasProfileIds = j.value("canvasProfileIds", std::vector<std::string>{});
    restored.m_outputProfileId  = j.value("outputProfileId",  std::string{});
    restored.rebuildLookupTables();

    m_project = std::move(restored);
}

} // namespace Platemaker::Infrastructure
