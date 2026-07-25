/**
 * \file lib/src/infrastructure/build_info/build_info.cpp
 * \brief Implementation of the build self-description and linked-component report.
 *
 * The version, compiler and platform are captured here — in a translation unit compiled *as part
 * of the library* — so the values are exactly those of the shipped DLL, not of any consumer.  The
 * compiler and target are read straight from the preprocessor: the compiler translating this file
 * is, by definition, the one that built the library.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-25
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/build_info/build_info.hpp>

#include <platemaker/version.hpp>

#include <string>

#include <nlohmann/json.hpp> // NLOHMANN_JSON_VERSION_* (build-time)
#include <vips/vips.h>       // vips_version() (runtime)

namespace Platemaker::Infrastructure {

namespace {

//! Compiler identity, resolved by the preprocessor of whoever compiles this TU.
//! Order matters: clang (and clang-cl) also define _MSC_VER / __GNUC__, so it is tested first.
std::string compilerString()
{
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown compiler";
#endif
}

//! Target OS and architecture, again from the preprocessor.
std::string platformString()
{
#if defined(_WIN32)
    std::string os = "Windows";
#elif defined(__linux__)
    std::string os = "Linux";
#elif defined(__APPLE__)
    std::string os = "macOS";
#else
    std::string os = "unknown OS";
#endif

#if defined(_M_X64) || defined(__x86_64__)
    std::string arch = "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    std::string arch = "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    std::string arch = "arm64";
#else
    std::string arch = "unknown arch";
#endif

    return os + ' ' + arch;
}

} // namespace

BuildInfo buildInfo()
{
    // The library's own SPDX licence, asserted here because the library owns this fact — it is the
    // same identifier carried in every source file's SPDX header. Consumers (About boxes, licence
    // notices) can then read it from the lib instead of hardcoding it.
    constexpr auto k_licence = "LGPL-3.0-or-later";

    return BuildInfo{
        std::string{version_string},
        k_licence,
        compilerString(),
        platformString(),
    };
}

std::vector<LinkedComponent> linkedComponents()
{
    const std::string vipsVersion = std::to_string(vips_version(0)) + '.' +
                                    std::to_string(vips_version(1)) + '.' +
                                    std::to_string(vips_version(2));

    const std::string jsonVersion = std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + '.' +
                                    std::to_string(NLOHMANN_JSON_VERSION_MINOR) + '.' +
                                    std::to_string(NLOHMANN_JSON_VERSION_PATCH);

    return {
        LinkedComponent{"libvips", vipsVersion, "LGPL-2.1-or-later",
                        "https://github.com/libvips/libvips"},
        LinkedComponent{"nlohmann/json", jsonVersion, "MIT",
                        "https://github.com/nlohmann/json"},
    };
}

} // namespace Platemaker::Infrastructure
