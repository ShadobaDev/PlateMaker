/**
 * \file lib/src/infrastructure/id_generator/id_generator.cpp
 * \brief Implementation of the workspace identifier generator.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/id_generator/id_generator.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace Platemaker::Infrastructure {

namespace {

//! Bounded retry count.  Unreachable with working randomness; guards against an
//! infinite loop if random_device degenerates to a constant sequence.
constexpr int k_maxAttempts = 8;

std::uint64_t draw()
{
    // thread_local: one engine per thread, seeded once, so concurrent callers never
    // share state and never repeat each other's sequence.
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine();
}

//! Builds "<prefix>-<hi><lo>" with both halves zero-padded to 16 hex digits.
std::string format(std::string_view prefix, std::uint64_t hi, std::uint64_t lo)
{
    std::ostringstream oss;
    oss << prefix << '-'
        << std::hex << std::setfill('0')
        << std::setw(16) << hi
        << std::setw(16) << lo;
    return oss.str();
}

/**
 * \brief Shared body of every unique-id helper.
 *
 * \tparam IsTaken Predicate invoked with a candidate id, returning true if it is in use.
 */
template <typename IsTaken>
std::string retryUntilFree(std::string_view prefix, const IsTaken& isTaken)
{
    for (int attempt = 0; attempt < k_maxAttempts; ++attempt) {
        std::string candidate = makeId(prefix);
        if (!isTaken(candidate)) return candidate;
    }

    throw std::runtime_error(
        "Platemaker::Infrastructure — could not find a free identifier for prefix '" +
        std::string{prefix} + "' after " + std::to_string(k_maxAttempts) +
        " attempts; the platform's random number source appears to be broken");
}

//! Predicate over any range whose elements expose a std::string \c id member.
template <typename Profiles>
auto idTakenIn(const Profiles& existing)
{
    return [&existing](const std::string& candidate) {
        return std::any_of(existing.begin(), existing.end(),
                           [&](const auto& p) { return p.id == candidate; });
    };
}

} // namespace

// ---------------------------------------------------------------------------
// makeId
// ---------------------------------------------------------------------------

std::string makeId(std::string_view prefix)
{
    return format(prefix, draw(), draw());
}

// ---------------------------------------------------------------------------
// makeUniqueId / makeUniqueCanvasProfileId / makeUniqueOutputProfileId
// ---------------------------------------------------------------------------

std::string makeUniqueId(std::string_view prefix, const std::vector<std::string>& taken)
{
    return retryUntilFree(prefix, [&taken](const std::string& candidate) {
        return std::find(taken.begin(), taken.end(), candidate) != taken.end();
    });
}

std::string makeUniqueCanvasProfileId(const std::vector<Models::CanvasProfile>& existing)
{
    return retryUntilFree("cp", idTakenIn(existing));
}

std::string makeUniqueOutputProfileId(const std::vector<Models::OutputProfile>& existing)
{
    return retryUntilFree("op", idTakenIn(existing));
}

} // namespace Platemaker::Infrastructure
