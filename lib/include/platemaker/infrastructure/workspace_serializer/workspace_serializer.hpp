/**
 * \file lib/include/platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp
 * \brief WorkspaceSerializer — reads and writes Workspace objects as .platemaker.json files.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP
#define PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/workspace.hpp>

namespace Platemaker::Infrastructure {

/**
 * \struct WorkspaceRepairReport
 * \brief What load() had to repair in a workspace file to make it self-consistent.
 *
 * Reports **identifier collisions only** — two or more profiles saved with the same \c id.
 * A workspace like that is not corrupt, but it is ambiguous: every lookup by that id
 * resolves to whichever profile comes first, so the other one is unreachable (it cannot be
 * assigned to a project, and anything already referencing the id may in fact have meant it).
 *
 * The repair keeps the **first** profile's id and mints a fresh one for each later duplicate,
 * so existing project references keep resolving and nothing is lost.
 *
 * \note Minting an id for a profile that simply had none is **not** reported.  That case is
 *       unambiguous — there are no two candidates to confuse — and reporting it would raise a
 *       warning on every pre-id workspace for no reason.
 *
 * \note Deliberately carries no list of affected projects.  Canvas profiles belong to the
 *       workspace rather than to a project, so such a list would have to name every project
 *       that ever touched the colliding id, and would still only be a *suspicion*.  Whether a
 *       project is genuinely stale is settled precisely by ProjectItem::sanitize(), which
 *       compares the canvas fingerprint recorded at render time.
 */
struct PLATEMAKER_EXPORT WorkspaceRepairReport {
    //! One profile that had to give up its colliding identifier.
    struct ReassignedProfile {
        std::string name;   //!< Profile name, for a message the user can act on.
        std::string oldId;  //!< The identifier it shared with an earlier profile.
        std::string newId;  //!< The freshly minted, unique identifier.
    };

    std::vector<ReassignedProfile> canvasProfiles; //!< Canvas profiles given a new id.
    std::vector<ReassignedProfile> outputProfiles; //!< Output profiles given a new id.

    //! \return true if anything at all was repaired.
    [[nodiscard]] bool any() const {
        return !canvasProfiles.empty() || !outputProfiles.empty();
    }
};

/**
 * \class WorkspaceSerializer
 * \brief Serialises and deserialises a Workspace to and from a UTF-8 JSON file.
 *
 * The on-disk format is a single JSON object with a top-level \c "version" integer
 * field.  When loading a file whose version is lower than the current schema version,
 * the serialiser runs the migration chain automatically before returning the object.
 *
 * WorkspaceSerializer is stateless and thread-safe for concurrent reads.
 * Concurrent writes to the same file are not supported — serialise write calls
 * externally if needed.
 *
 * \note The JSON library used internally is nlohmann/json (MIT licence).
 */
class PLATEMAKER_EXPORT WorkspaceSerializer {
public:
    WorkspaceSerializer() = default;

    /**
     * \brief Loads a Workspace from a .platemaker.json file.
     *
     * If the file's schema version is older than the current version, each
     * intermediate migration step is applied in sequence before returning.
     *
     * \param filePath Absolute path to the .platemaker.json file.
     * \return A fully populated Workspace object.
     *
     * \throws std::runtime_error if the file cannot be opened, is not valid JSON,
     *                            or fails a required schema field validation.
     */
    [[nodiscard]] Models::Workspace load(const std::string& filePath) const;

    /**
     * \brief Loads a Workspace and reports any identifier collisions that had to be repaired.
     *
     * Identical to the single-argument overload in every respect — the repair happens either
     * way — except that this one tells the caller about it, so a GUI can explain to the user
     * why a profile reappeared and why projects may now need a refresh.
     *
     * \param filePath Absolute path to the .platemaker.json file.
     * \param report   Filled in with the repairs performed; cleared first.
     * \return A fully populated Workspace object.
     *
     * \throws std::runtime_error under the same conditions as the single-argument overload.
     *
     * \see WorkspaceRepairReport
     */
    [[nodiscard]] Models::Workspace load(const std::string&      filePath,
                                         WorkspaceRepairReport&  report) const;

    /**
     * \brief Saves a Workspace to a .platemaker.json file.
     *
     * The file is written atomically (written to a temporary file then renamed)
     * to avoid corruption on unexpected termination.
     *
     * \param workspace The Workspace object to serialise.
     * \param filePath  Absolute path of the destination file.  Created if it does
     *                  not exist; overwritten if it does.
     *
     * \throws std::runtime_error if the file cannot be written.
     */
    void save(
        const Models::Workspace& workspace,
        const std::string&       filePath) const;

    /**
     * \brief Serialises a Workspace to its canonical JSON string form.
     *
     * Produces exactly the text that \c save() would write to disk.  Useful for
     * change detection (compare against a saved snapshot) without touching the
     * filesystem.
     *
     * \param workspace The Workspace object to serialise.
     * \return The pretty-printed JSON representation.
     */
    [[nodiscard]] std::string serialize(const Models::Workspace& workspace) const;

private:
    /**
     * \brief Runs the migration chain to bring an old workspace up to the current version.
     *
     * Called automatically by \c load() when the file version is below the current
     * schema version.  Each migration step is a small function that transforms the
     * JSON object in-place from version N to N+1.
     *
     * \param workspace   The workspace loaded from disk (potentially old schema).
     * \param fromVersion The schema version read from the file.
     */
    void migrate(Models::Workspace& workspace, int fromVersion) const;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP
