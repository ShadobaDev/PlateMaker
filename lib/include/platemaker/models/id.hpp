/**
 * \file lib/include/platemaker/models/id.hpp
 * \brief Identifier generation for workspace entities (canvas profiles, output profiles, projects).
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_ID_HPP
#define PLATEMAKER_MODELS_ID_HPP

#include <string>
#include <string_view>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Models {

/**
 * \brief Generates a random identifier of the form \c "<prefix>-<32 hex digits>".
 *
 * 128 bits drawn from a thread-local \c std::mt19937_64 seeded by \c std::random_device.
 *
 * \warning This is the raw primitive and guarantees **nothing** about uniqueness — it only
 *          makes a collision astronomically unlikely.  "Astronomically unlikely" is exactly
 *          the reasoning that produced the duplicate-ID bug this function replaces (IDs used
 *          to be a millisecond timestamp, and a loop mints several within one millisecond).
 *          Prefer the \c makeUnique*Id() helpers, which check the result against the
 *          identifiers already in use.
 *
 * \param prefix Short tag kept for readability in the workspace JSON (\c "cp", \c "op", \c "proj").
 * \return e.g. \c "cp-4b7e4d519a080c2e0c2e5f7b1d449a08"
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string makeId(std::string_view prefix);

/**
 * \brief Returns an identifier that does not appear in \p taken.
 *
 * The general form, for entities the typed helpers below do not cover (project uuids, for
 * instance, whose field is named \c uuid rather than \c id).
 *
 * \param prefix Short tag, as for makeId().
 * \param taken  Identifiers already in use.
 * \return An identifier absent from \p taken.
 * \throws std::runtime_error if no free identifier is found in a bounded number of attempts.
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string makeUniqueId(
    std::string_view prefix, const std::vector<std::string>& taken);

/**
 * \brief Returns a \c "cp-" identifier that no profile in \p existing is using.
 *
 * Draws from makeId() and rejects anything already taken.  Since an ID is minted once per
 * profile and a workspace holds a handful of them, the linear scan is free and turns a
 * probabilistic guarantee into a hard one.
 *
 * \param existing The workspace's current canvas profiles.
 * \return An identifier unique within \p existing.
 * \throws std::runtime_error if no free identifier is found in a bounded number of attempts,
 *         which in practice only happens if the platform's randomness is broken.
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string makeUniqueCanvasProfileId(
    const std::vector<CanvasProfile>& existing);

/**
 * \brief Returns an \c "op-" identifier that no profile in \p existing is using.
 * \see makeUniqueCanvasProfileId
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string makeUniqueOutputProfileId(
    const std::vector<OutputProfile>& existing);

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_ID_HPP
