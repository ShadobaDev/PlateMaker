/**
 * \file lib/include/platemaker/infrastructure/workspace_editor/workspace_editor.hpp
 * \brief WorkspaceEditor — the single authority that mutates a Workspace's profile palettes
 *        while enforcing every workspace invariant.
 *
 * \c Models::Workspace holds its canvas and output profiles in private vectors; the only way
 * to change them is through this facade.  That closes a long-standing hole: the invariant
 * rules (unique ids, no persisted presets, \c templateInfo carried across a bulk replace) used
 * to live only in \c WorkspaceSerializer::load(), reachable solely through a save→load round
 * trip, so an in-session edit made by the CLI or GUI was never validated the way a loaded file
 * was.  \c WorkspaceEditor and \c load() now run the *same* code — \c load() delegates the
 * repair pass to \c installLoaded().
 *
 * Lives in Infrastructure, not Models, on purpose: enforcing the invariants means minting ids
 * (\c id_generator), and Models is deliberately kept free of any Infrastructure dependency.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_WORKSPACE_EDITOR_HPP
#define PLATEMAKER_INFRASTRUCTURE_WORKSPACE_EDITOR_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp> // WorkspaceRepairReport
#include <platemaker/models/workspace.hpp>

namespace Platemaker::Infrastructure {

/**
 * \class WorkspaceEditor
 * \brief Intent-level, invariant-enforcing edits to a \c Models::Workspace.
 *
 * A transient bound to a \c Workspace& for the duration of an edit — it holds no state beyond
 * the reference, so construct one where you need it and let it go.  It is the sole \c friend of
 * \c Workspace with write access to the profile palettes.
 *
 * \note All palette operations keep the "presets are never persisted / ids are unique" model
 *       intact; the load path (\c installLoaded) runs the full repair pass so a workspace read
 *       from disk and a workspace edited in memory obey the same rules.
 */
class PLATEMAKER_EXPORT WorkspaceEditor {
public:
    /// Binds to \p ws; the reference must outlive the editor.
    explicit WorkspaceEditor(Models::Workspace& ws) noexcept : m_ws(ws) {}

    // -----------------------------------------------------------------------
    // Canvas profile palette
    // -----------------------------------------------------------------------

    /**
     * \brief Adds a canvas profile to the workspace palette, giving it a fresh unique id.
     * \param p Profile to add; any id it carries is overwritten.
     * \return The minted id.
     */
    std::string addCanvasProfile(Models::CanvasProfile p);

    /**
     * \brief Removes the canvas profile with \p id from the palette.
     *
     * Project references to the removed id are left in place: they resolve to nothing and are
     * dropped by \c ProjectItem::effectiveCanvasProfileIds() / \c sanitize(), so no dangling
     * link can affect a render.
     *
     * \return \c true if a profile was removed, \c false if \p id was not present.
     */
    bool removeCanvasProfile(const std::string& id);

    /**
     * \brief Replaces the whole canvas palette (e.g. the result of a "manage profiles" dialog).
     *
     * Carries \c templateInfo from the current profile of the same id onto the incoming one
     * (the workspace is the source of truth — the manage dialog drops the field on its round
     * trip), mints ids for any profile that arrives without one, then deduplicates.
     *
     * \param incoming The new palette.
     * \return Identifier collisions that had to be repaired (empty when there were none).
     */
    WorkspaceRepairReport replaceCanvasProfiles(std::vector<Models::CanvasProfile> incoming);

    /**
     * \brief Sets (or clears) the generated-template metadata of one canvas profile.
     *
     * The precise counterpart to \c replaceCanvasProfiles's "carry templateInfo across a dialog
     * round trip": template generation and deletion need to write an *exact* value — including an
     * empty one to clear it — which a carry heuristic cannot express (a cleared field looks the same
     * as a dropped one). Touches only \c templateInfo; every other field is left as-is.
     *
     * \param id   Profile to update.
     * \param info The new template metadata (default-constructed to clear it).
     * \return \c true if a profile with \p id was found and updated, \c false otherwise.
     */
    bool setCanvasProfileTemplateInfo(const std::string& id, Models::CanvasTemplateInfo info);

    // -----------------------------------------------------------------------
    // Output profile palette
    // -----------------------------------------------------------------------

    /**
     * \brief Adds an output profile to the workspace palette, giving it a fresh unique id.
     * \return The minted id (always a user id — never a reserved preset id).
     */
    std::string addOutputProfile(Models::OutputProfile p);

    /// \brief Removes the output profile with \p id. \return \c true if one was removed.
    bool removeOutputProfile(const std::string& id);

    /**
     * \brief Replaces the whole output palette.
     *
     * Drops any profile carrying a baked-in preset id (presets live in code and must never be
     * persisted), mints missing ids, then deduplicates.
     *
     * \param incoming The new palette (user profiles; presets are filtered out).
     * \return Identifier collisions that had to be repaired.
     */
    WorkspaceRepairReport replaceOutputProfiles(std::vector<Models::OutputProfile> incoming);

    // -----------------------------------------------------------------------
    // Projects
    // -----------------------------------------------------------------------

    /**
     * \brief Creates a project with a fresh, workspace-unique uid and appends it to the workspace.
     *
     * Minting the project uid is the lib's job, not the consumer's: the uid must be unique across
     * every project in the workspace, so the workspace is the only place that can guarantee it.
     * (Input/output uids are minted by \c ProjectItem::ensureUniqueFileUids(); profile ids by the
     * palette ops above — this closes the last identifier a consumer used to hand-roll.)
     *
     * \param name Human-readable project name.
     * \return Reference to the newly appended project. Valid until the next mutation of the
     *         workspace's project list; the caller typically populates it immediately.
     */
    Models::ProjectItem& addProject(std::string name);

    /**
     * \brief Creates a new project seeded from \p source — its input files and profile links only.
     *
     * A deliberately **naive** copy, not a render clone: it reproduces what defines the project's
     * configuration and drops everything the source *produced*. Copied — the input file list (paths +
     * strip order) and the profile links (\c canvasProfileIds + \c outputProfileId). Dropped — the
     * output directory, the output slice list, and all render state (per-input hashes/status, canvas
     * fingerprints, \c outputSignature and the render baselines). The copy's inputs therefore start
     * \c Pending, so it renders from scratch into its own output folder.
     *
     * This is exactly what the multi-publisher workflow needs: sibling projects over the same pages
     * that differ only in the output profile, each writing to its own directory. Clearing the output
     * directory is intentional — two projects sharing one folder would overwrite each other's slices.
     *
     * The project uid is minted fresh and workspace-unique (only the workspace can guarantee that, as
     * for \c addProject); the copied inputs get fresh project-local uids via
     * \c ProjectItem::ensureUniqueFileUids().
     *
     * \param source  The project to seed from (typically one already in this workspace).
     * \param newName Name for the new project.
     * \return Reference to the newly appended project. Valid until the next mutation of the
     *         workspace's project list.
     */
    Models::ProjectItem& duplicateProject(const Models::ProjectItem& source, std::string newName);

    // -----------------------------------------------------------------------
    // Project ↔ profile links
    // -----------------------------------------------------------------------

    /**
     * \brief Links a canvas profile to a project, through the conflict guard.
     *
     * Delegates to \c ProjectItem::addCanvasProfile(); rejects a profile whose canvas W×H
     * collides with one already linked (SPECIFICATION.md §7.5.2).
     *
     * \return \c true on success, \c false if the id is unknown or conflicts.
     */
    bool addCanvasProfileToProject(Models::ProjectItem& project, const std::string& profileId);

    /**
     * \brief Unlinks a canvas profile from a project — the symmetric partner of the guarded add.
     * \return \c true if the profile was linked and is now removed, \c false if it was not linked.
     */
    bool removeCanvasProfileFromProject(Models::ProjectItem& project, const std::string& profileId);

    /**
     * \brief Sets a project's output profile, validating that the id resolves.
     *
     * An empty id is accepted and means "use the workspace default".  A non-empty id must name
     * a user profile or a baked-in preset (checked via \c resolveOutputProfile()).
     *
     * \return \c true if set, \c false if \p outputProfileId is non-empty and resolves to nothing.
     */
    bool setProjectOutputProfile(Models::ProjectItem& project, const std::string& outputProfileId);

    // -----------------------------------------------------------------------
    // Load path
    // -----------------------------------------------------------------------

    /**
     * \brief Installs freshly-parsed profile palettes and runs the load-time repair pass.
     *
     * Called by \c WorkspaceSerializer::load(): it moves the parsed vectors into the workspace,
     * then mints missing ids, deduplicates, and drops persisted presets — the single copy of the
     * rules a loaded file is put through.  Always runs the full repair, so it cannot be used to
     * smuggle an invalid palette in.
     *
     * \param canvas The parsed canvas profiles (moved in).
     * \param output The parsed output profiles (moved in).
     * \param report Filled with any identifier collisions repaired; cleared first.
     */
    void installLoaded(std::vector<Models::CanvasProfile>&& canvas,
                       std::vector<Models::OutputProfile>&& output,
                       WorkspaceRepairReport&               report);

    // -----------------------------------------------------------------------
    // Snapshot / restore (undo/redo support) — workspace-level metadata only
    // -----------------------------------------------------------------------

    /**
     * \brief Serialises the workspace-level metadata to an opaque snapshot string.
     *
     * Captures only what is owned at the workspace scope: the canvas and output profile palettes
     * (presets filtered, as they are never persisted) and the project roster as \c (uid, name) pairs.
     * It deliberately does **not** include project *contents* (inputs/outputs) — those are captured
     * per project by \c ProjectEditor::snapshot(), which keeps this snapshot small and keeps a
     * workspace-scope undo from resurrecting project content reverted on a project's own timeline.
     */
    [[nodiscard]] std::string snapshotMeta() const;

    /**
     * \brief Restores workspace-level metadata from a string produced by \c snapshotMeta().
     *
     * Reinstalls the profile palettes through the validated palette setters (ids preserved, presets
     * stripped, \c templateInfo restored to the exact snapshot value) and restores each project's
     * \c name by uid. Project contents and the project roster (add/remove) are left untouched — undo
     * of add/remove is out of scope, so the set of projects a snapshot describes still exists.
     *
     * \param snapshot A string previously returned by \c snapshotMeta().
     * \throws nlohmann::json::exception if \p snapshot is not valid metadata JSON.
     */
    void restoreMeta(const std::string& snapshot);

private:
    // --- invariant helpers (one copy of the rules, shared by the ops above and installLoaded) ---

    /// Rewrites every project reference to \p oldId so it points at \p newId instead.
    static void relinkProfileId(Models::Workspace& ws,
                                const std::string& oldId,
                                const std::string& newId);

    /// Gives a fresh unique id to every profile saved without one, relinking legacy references.
    static void mintMissingProfileIds(Models::Workspace& ws);

    /// Removes profiles that were persisted as presets (see the "presets never persisted" model).
    static void migrateOutputProfilePresets(Models::Workspace& ws);

    /// Breaks up shared identifiers: first holder keeps the id, later duplicates get a fresh one.
    template <typename Profiles, typename MakeId>
    static void deduplicateIds(Profiles&                                              profiles,
                               const MakeId&                                          makeFreshId,
                               std::vector<WorkspaceRepairReport::ReassignedProfile>& out);

    Models::Workspace& m_ws;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_WORKSPACE_EDITOR_HPP
