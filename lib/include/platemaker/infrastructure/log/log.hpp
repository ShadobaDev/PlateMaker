/**
 * \file lib/include/platemaker/infrastructure/log/log.hpp
 * \brief Lightweight, component-gated diagnostic logger for libplatemaker.
 *
 * A single process-wide facility every library component can log through. Output is gated by
 * a runtime bitmask: each component owns one bit of a \c uint64 (up to 64 components), and a
 * message is emitted only when that component's bit is set in the enabled mask. The mask
 * defaults to 0, so the library is completely silent unless a host opts specific components in
 * at runtime — the CLI does so from its \c --trace=0x... argument, a test from setEnabledComponents(),
 * the GUI could from a debug menu. There are deliberately **no severity levels** — only the
 * per-component on/off gate.
 *
 * This is a developer diagnostic and is intentionally *not* the library's user-facing message
 * channel: run progress, warnings and errors reach a consumer through
 * ProcessingCallbacks::onLog. Keep the two separate.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-17
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_LOG_LOG_HPP
#define PLATEMAKER_INFRASTRUCTURE_LOG_LOG_HPP

#include "platemaker/platemaker_export.h"

#include <cstdint>
#include <functional>
#include <string_view>

/**
 * \namespace Platemaker::Infrastructure::Log
 * \brief Process-wide, component-gated diagnostic logging (see \ref log.hpp).
 */
namespace Platemaker::Infrastructure::Log {

/**
 * \brief Component identifiers — one bit each in the \c uint64 enabled-mask (max 64 components).
 *
 * The numeric bit is the wire format of the CLI's \c --trace=0x... argument, so the values are
 * stable: append new components at the next free bit, never renumber an existing one. \c None
 * and \c All are convenience masks, not components.
 */
enum Component : std::uint64_t {
    None                 = 0,
    ProcessingPipeline   = 1ull << 0,   // 0x0001
    Scaler               = 1ull << 1,   // 0x0002
    ScaledStrip          = 1ull << 2,   // 0x0004
    MarginCropper        = 1ull << 3,   // 0x0008
    CanvasProfileMatcher = 1ull << 4,   // 0x0010
    ImageIO              = 1ull << 5,   // 0x0020
    TemplateGenerator    = 1ull << 6,   // 0x0040
    WorkspaceSerializer  = 1ull << 7,   // 0x0080
    WorkspaceEditor      = 1ull << 8,   // 0x0100
    ProjectEditor        = 1ull << 9,   // 0x0200
    ThumbnailCache       = 1ull << 10,  // 0x0400
    FileMetaData         = 1ull << 11,  // 0x0800
    ColourCorrector      = 1ull << 12,  // 0x1000
    All                  = ~0ull,
};

/// Replace the enabled-component mask (thread-safe). Bits set → those components log.
PLATEMAKER_EXPORT void setEnabledComponents(std::uint64_t mask) noexcept;

/// The current enabled-component mask (thread-safe).
[[nodiscard]] PLATEMAKER_EXPORT std::uint64_t enabledComponents() noexcept;

/// OR extra components into the mask (thread-safe).
PLATEMAKER_EXPORT void enable(std::uint64_t components) noexcept;

/// Clear components from the mask (thread-safe).
PLATEMAKER_EXPORT void disable(std::uint64_t components) noexcept;

/// True if any bit of \p component is currently enabled (thread-safe).
[[nodiscard]] PLATEMAKER_EXPORT bool isEnabled(std::uint64_t component) noexcept;

/// Sink signature: (component bit, message).
using Sink = std::function<void(std::uint64_t component, std::string_view message)>;

/// Replace the sink (thread-safe). An empty sink restores the default (one line per message to stderr).
PLATEMAKER_EXPORT void setSink(Sink sink);

/// Human-readable name for a single component bit (used by the default sink and diagnostics).
[[nodiscard]] PLATEMAKER_EXPORT const char* componentName(std::uint64_t component) noexcept;

/**
 * \brief Emit \p message for \p component if it is enabled.
 *
 * Prefer the \c PLATEMAKER_LOG macro, which skips building \p message entirely when the
 * component is disabled; call this directly only when the message is already in hand.
 */
PLATEMAKER_EXPORT void write(std::uint64_t component, std::string_view message);

} // namespace Platemaker::Infrastructure::Log

/**
 * \def PLATEMAKER_LOG
 * \brief Log \p message for \p component, building \p message only when the component is enabled.
 *
 * \p message is any single expression convertible to \c std::string_view (typically an assembled
 * \c std::string, or an IIFE returning one). It is evaluated only when \p component is enabled, so
 * the common disabled path costs a single relaxed atomic load.
 */
#define PLATEMAKER_LOG(component, message)                                       \
    do {                                                                         \
        if (::Platemaker::Infrastructure::Log::isEnabled((component)))           \
            ::Platemaker::Infrastructure::Log::write((component), (message));    \
    } while (0)

#endif // PLATEMAKER_INFRASTRUCTURE_LOG_LOG_HPP
