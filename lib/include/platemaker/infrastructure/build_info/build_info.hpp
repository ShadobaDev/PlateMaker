/**
 * \file lib/include/platemaker/infrastructure/build_info/build_info.hpp
 * \brief Runtime self-description of this build: its version, its toolchain, and what it links.
 *
 * Two facts about a shipped shared library can only be answered honestly from *inside* the
 * library, and both live here.
 *
 * **Why runtime, not the header.**  \c platemaker/version.hpp is a compile-time constant: it is
 * baked into whoever includes it, so it reports the header a consumer *built against*, not the
 * DLL that actually loaded.  Replace only the DLL and the header keeps asserting the old version,
 * with no way to notice the skew.  The mature-C-library answer is to expose the version twice on
 * purpose — a compile-time macro for \c #if / \c find_package, and a runtime function for the
 * loaded truth (zlib \c ZLIB_VERSION vs \c zlibVersion(), libvips \c VIPS_MAJOR_VERSION vs
 * \c vips_version(), SQLite, OpenSSL, libcurl all do this).  \c buildInfo() is that runtime twin,
 * and \c runtimeMatchesHeader() is zlib's consistency check: compare the two and flag a mismatch.
 *
 * **Why the lib owns the linked-component list.**  A consumer that hardcodes "libvips,
 * LGPL-2.1-or-later" is asserting a fact about the lib's dependencies that it cannot know — swap
 * the backend or bump libvips across a licence change and the consumer shows stale information.
 * Only the lib knows what it linked, and LGPL-2.1 *requires* naming what is used and under what
 * terms, so \c linkedComponents() reports it — libvips at its runtime version, so an app that
 * ships and can swap its own \c libvips-42.dll gets the honest loaded answer.
 *
 * Lives in Infrastructure for the same reason as the id generator: it reaches into the
 * platform/toolchain layer (the compiler that built this translation unit, the loaded libvips)
 * rather than describing the data model.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-25
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_BUILD_INFO_HPP
#define PLATEMAKER_INFRASTRUCTURE_BUILD_INFO_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/version.hpp>

namespace Platemaker::Infrastructure {

/**
 * \brief How this shared library was built, read back from the library itself at runtime.
 *
 * Every field is baked into the DLL when the library compiles, so it travels *inside* the shared
 * object — the source of truth for "what actually loaded", independent of any consumer header.
 */
struct BuildInfo {
    std::string version;   //!< The DLL's own version — the runtime twin of \c version.hpp.
    std::string compiler;  //!< Compiler that built the library, e.g. \c "GCC 14.2.0", \c "MSVC 1940".
    std::string platform;  //!< Target OS and architecture, e.g. \c "Windows x86_64".
};

/**
 * \brief Returns this build's own version, toolchain and target — the runtime counterpart to the
 *        compile-time values in \c platemaker/version.hpp.
 */
[[nodiscard]] PLATEMAKER_EXPORT BuildInfo buildInfo();

/**
 * \brief One third-party component this build of libplatemaker links.
 *
 * For About boxes and licence notices.  \c version carries libvips' *runtime* version where the
 * dependency exposes one, otherwise the value fixed at build time.
 */
struct LinkedComponent {
    std::string name;     //!< e.g. \c "libvips".
    std::string version;  //!< Runtime version where available, else build-time.
    std::string licence;  //!< SPDX identifier, e.g. \c "LGPL-2.1-or-later".
};

/**
 * \brief The third-party components this build links, with versions and SPDX licences.
 *
 * Reports **libvips** (LGPL-2.1-or-later, runtime version via \c vips_version()) and
 * **nlohmann/json** (MIT, build-time version).  GoogleTest is test-only and never shipped, so it
 * is not listed.
 */
[[nodiscard]] PLATEMAKER_EXPORT std::vector<LinkedComponent> linkedComponents();

/**
 * \brief True when the loaded library's version matches the header this call was compiled against.
 *
 * Inline on purpose — it is *not* an exported symbol.  It compares the consumer's compile-time
 * \c version_string against the runtime \c buildInfo().version returned from the DLL, so a mismatch
 * means the loaded library differs from the header the caller built with (a swapped DLL, or a
 * header out of sync with the linked import library).  This is zlib's \c ZLIB_VERSION /
 * \c zlibVersion() consistency check.
 *
 * \note Exact-string comparison.  On the pre-1.0 shifted scale a breaking change bumps the minor,
 *       so any differing component is worth flagging; a consumer wanting a looser rule can compare
 *       a prefix of the two strings itself.
 */
[[nodiscard]] inline bool runtimeMatchesHeader()
{
    return buildInfo().version == std::string{version_string};
}

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_BUILD_INFO_HPP
