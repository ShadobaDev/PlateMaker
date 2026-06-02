/**
 * \file lib/include/platemaker/models/canvas_profile.hpp
 * \brief CanvasProfile data model — describes a named canvas with margin and visual settings.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_CANVAS_PROFILE_HPP
#define PLATEMAKER_MODELS_CANVAS_PROFILE_HPP

#include <string>

#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/**
 * \class CanvasProfile
 * \brief A named canvas configuration that captures the full canvas size, margin
 *        offsets, and the visual colour used for template overlay rendering.
 *
 * The artist sets up one CanvasProfile per canvas type they work with (e.g. a
 * four-page Webtoon canvas, a traditional manga page, etc.).  The profile is
 * stored inside the workspace and drives both the template generator and the
 * margin-aware import pipeline.
 *
 * The \c safeArea is a computed, read-only property; it is never serialised
 * directly — it is always derived from \c canvasSize and \c margins on the fly.
 */
class CanvasProfile {
public:
    // ---------------------------------------------------------------------------
    // Data members
    // ---------------------------------------------------------------------------

    /**
     * \brief Human-readable profile name shown in the GUI and used for workspace lookup.
     *
     * Examples: "Webtoon 4-page", "Marvel Standard", "Manga B4".
     */
    std::string name;

    Size    canvasSize;   //!< Full canvas dimensions including all margin zones, in pixels.
    Margins margins;      //!< Margin widths on each side, in pixels.

    /**
     * \brief Overlay colour used by TemplateGenerator to highlight margin zones.
     *
     * Typically a semi-transparent pink/magenta, e.g. {255, 105, 180, 128}.
     */
    RGBA visualColour = {255, 105, 180, 128};

    // ---------------------------------------------------------------------------
    // Computed properties
    // ---------------------------------------------------------------------------

    /**
     * \brief Returns the safe-area size (canvas minus all margins).
     *
     * The safe area is the region within which the artist should draw to avoid
     * content being cropped by the margin pipeline.
     *
     * \return Size The pixel dimensions of the safe drawing area.
     */
    [[nodiscard]] Size safeArea() const noexcept;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_CANVAS_PROFILE_HPP
