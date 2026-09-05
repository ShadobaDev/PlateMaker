/**
 * \file lib/include/platemaker/models/workspace_repair_report.hpp
 * \brief WorkspaceRepairReport - what loading a workspace had to fix to make it unambiguous.
 *
 * Split out of \c workspace_serializer.hpp because it is not the serializer's private business:
 * \c WorkspaceEditor returns the same type from its replace/import operations, so a header that
 * belonged to one of the two producers made the other look like it was borrowing.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_WORKSPACE_REPAIR_REPORT_HPP
#define PLATEMAKER_MODELS_WORKSPACE_REPAIR_REPORT_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

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
    [[nodiscard]] bool anyRepairs() const {
        return !canvasProfiles.empty() || !outputProfiles.empty();
    }
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_MODELS_WORKSPACE_REPAIR_REPORT_HPP
