/**
 * \file lib/src/infrastructure/file/folder_paths.hpp
 * \brief Paths in and relative to a workspace folder — internal, shared by the serializer and the packager.
 *
 * Both answer the same question — "does this file lie in the workspace's folder, and where in it?" — the
 * serializer to write a path that survives the folder moving, the packager to keep a file where it was in
 * the folder. One answer, so the two cannot disagree about which files are the folder's.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_FILE_FOLDER_PATHS_HPP
#define PLATEMAKER_INFRASTRUCTURE_FILE_FOLDER_PATHS_HPP

#include <filesystem>
#include <string>

namespace Platemaker::Infrastructure {

/// UTF-8 with '/' separators, normalised — the form consumers write paths in.
[[nodiscard]] inline std::string genericUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.lexically_normal().generic_u8string();
    return std::string(s.begin(), s.end());
}

/**
 * \brief \p file relative to \p folder, '/'-separated, when it lies inside it; empty otherwise.
 *
 * Compared as written: both paths should be absolute and spelled alike (see the caller for how).
 */
[[nodiscard]] inline std::string relativeInside(const std::filesystem::path& file, const std::filesystem::path& folder)
{
    if (!file.is_absolute())
        return {};
    const std::filesystem::path rel = file.lexically_normal().lexically_relative(folder.lexically_normal());
    if (rel.empty() || rel == "." || *rel.begin() == "..")
        return {};
    return genericUtf8(rel);
}

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_FILE_FOLDER_PATHS_HPP
