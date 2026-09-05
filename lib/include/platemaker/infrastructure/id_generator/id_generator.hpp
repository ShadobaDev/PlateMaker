/**
 * \file lib/include/platemaker/infrastructure/id_generator/id_generator.hpp
 * \brief Identifier generation for workspace entities (canvas profiles, output profiles, projects).
 *
 * Lives in Infrastructure rather than Core or Models on purpose.  It is not part of the data
 * model — it produces values rather than describing them — and it is not Core either, because
 * Core is deterministic domain logic (CanvasProfileMatcher, MarginCropper, Scaler all return
 * the same answer for the same input).  This draws on \c std::random_device, a platform
 * entropy source, which makes it a service from the outside world in the same sense as the
 * clock or the filesystem.  CancellationToken sets the precedent: Infrastructure here means
 * platform-facing plumbing, not strictly file I/O.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_ID_GENERATOR_HPP
#define PLATEMAKER_INFRASTRUCTURE_ID_GENERATOR_HPP

#include <string>
#include <string_view>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/output_presets.hpp>

namespace Platemaker::Infrastructure {

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
 * The general form, for entities the typed helpers below do not cover (project / input / output
 * uids, for instance, whose field is named \c uid rather than \c id).
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
    const std::vector<Models::CanvasProfile>& existing);

/**
 * \brief Returns an \c "op-" identifier that no profile in \p existing is using.
 *
 * \note Never returns a preset identifier: presets use the reserved \c "op-preset-" prefix
 *       (see \c Models::outputProfilePresets()) and generated ids are hex, so the two
 *       namespaces cannot overlap.
 *
 * \see makeUniqueCanvasProfileId
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string makeUniqueOutputProfileId(
    const std::vector<Models::OutputProfile>& existing);

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_ID_GENERATOR_HPP
