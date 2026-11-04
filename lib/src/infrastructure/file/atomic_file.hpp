/**
 * \file lib/src/infrastructure/file/atomic_file.hpp
 * \brief A file written whole or not at all — internal, shared by every writer that replaces a file.
 *
 * Each writer (the workspace, a profile bundle, a package, an output slice, a thumbnail) writes into a
 * temporary sibling and then moves it over the destination, so a reader never sees half a file and a
 * failed write leaves the old one untouched. How they write differs; the publishing does not, and neither
 * does the rule that a failure leaves no temporary file behind.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_FILE_ATOMIC_FILE_HPP
#define PLATEMAKER_INFRASTRUCTURE_FILE_ATOMIC_FILE_HPP

#include <filesystem>
#include <string>
#include <system_error>

namespace Platemaker::Infrastructure {

/**
 * \brief Moves \p temp over \p target; on a failure \p temp is removed and the error returned.
 *
 * A rename replaces the destination in one step. It fails on Windows while another program holds the
 * destination open without letting it be replaced; with \p copyFallback the bytes are then copied over it
 * instead, which works when that program let it be written. Without, the caller reports the held file
 * (an output slice: OutputLockedError, the consumer's to retry). The temporary file is a sibling, so a
 * rename never crosses devices.
 *
 * \return Empty when \p target holds the new bytes.
 */
[[nodiscard]] std::error_code publishTempFile(const std::filesystem::path& temp,
                                              const std::filesystem::path& target,
                                              bool                         copyFallback);

/**
 * \brief Writes \p text to \p target through a temporary sibling, `<target>.tmp`, with the copy fallback.
 *
 * \throws std::runtime_error naming \p who when the temporary file cannot be opened or written (a full
 *         disk shows on closing it), or cannot be moved over \p target. Nothing is left beside \p target.
 */
void writeTextFileAtomically(const std::filesystem::path& target, const std::string& text, const std::string& who);

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_FILE_ATOMIC_FILE_HPP
