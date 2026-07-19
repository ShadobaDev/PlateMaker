/**
 * \file lib/include/platemaker/infrastructure/file/path_utf8.hpp
 * \brief Explicit UTF-8 ↔ std::filesystem::path conversion for every filesystem boundary.
 *
 * Every path in libplatemaker's API is UTF-8: workspace JSON stores UTF-8 (nlohmann), and the
 * GUI hands over UTF-8 via QString::toStdString().  What a *narrow* std::string means to the
 * standard library, however, is not agreed upon:
 *
 * - libstdc++ on MinGW reads a narrow string in std::filesystem::path as UTF-8 and converts
 *   it to UTF-16, so fs::exists() works;
 * - std::ifstream(std::string) goes straight to fopen(), where the CRT reads the same bytes
 *   in the **active ANSI code page**, so the file is not found;
 * - MSVC treats narrow strings as ANSI in *both* places.
 *
 * The result was a program using two incompatible conventions for one string. On a path such
 * as "G:/Mój dysk/…" the render succeeded (libvips takes UTF-8 and lets GLib convert it) while
 * hashing the very same file silently returned nothing — which left every input stuck on
 * Pending and made each render redo all the work. The same split inside WorkspaceSerializer,
 * where save() opened through an fs::path and load() through a std::string, produced a
 * workspace that could be written once and never reopened.
 *
 * These helpers make the conversion explicit, so behaviour no longer depends on the toolchain
 * or on the machine's code page. Nothing in the library may build an fs::path from a narrow
 * string directly.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_PATH_UTF8_HPP
#define PLATEMAKER_INFRASTRUCTURE_PATH_UTF8_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace Platemaker::Infrastructure {

/**
 * \brief Builds a filesystem path from a UTF-8 string.
 *
 * \param utf8 A path encoded in UTF-8 — the only encoding this library's API uses.
 * \return The corresponding native path.
 *
 * \note Goes through \c std::u8string, whose meaning C++20 fixes as UTF-8. Constructing
 *       \c fs::path from a plain \c std::string would instead mean "whatever this
 *       implementation calls narrow", which is exactly the ambiguity being removed.
 */
[[nodiscard]] inline std::filesystem::path utf8ToPath(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/**
 * \brief Renders a filesystem path back as UTF-8.
 *
 * \param p The path to convert.
 * \return Its UTF-8 representation.
 *
 * \warning Never use \c path::string() for this. On MSVC it encodes in the active ANSI code
 *          page and throws on a character that page cannot represent — so a path that opened
 *          fine would fail on the way back out.
 */
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_PATH_UTF8_HPP
