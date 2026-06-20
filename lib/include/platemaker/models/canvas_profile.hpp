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
#include "platemaker/platemaker_export.h"
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/**
 * \struct CanvasTemplateInfo
 * \brief Bookkeeping for the PNG template last generated from a CanvasProfile.
 *
 * Stored inside the workspace (per canvas profile) so a workspace tracks only its
 * own templates.  \c path is kept **relative to the workspace directory** so the
 * workspace folder stays portable.  An empty \c path means "no template generated
 * yet".
 *
 * \c fingerprint is a canvas-only signature (see TemplateGenerator::signature())
 * captured at generation time.  Comparing it against the current profile's
 * signature tells the GUI whether the template is still up to date — the output
 * profile is deliberately not part of a template's identity.
 */
struct CanvasTemplateInfo {
    std::string path;        //!< Template PNG path, relative to the workspace dir (empty = none).
    std::string fingerprint; //!< Canvas-only signature at generation time.
    std::string generatedAt; //!< ISO 8601 timestamp of the last generation.
};

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
class PLATEMAKER_EXPORT CanvasProfile {
public:
    // ---------------------------------------------------------------------------
    // Data members
    // ---------------------------------------------------------------------------

    std::string id;   //!< Stable unique identifier — never changes after creation.

    /**
     * \brief Human-readable profile name shown in the GUI and used for workspace lookup.
     *
     * Examples: "Webtoon 4-page", "Marvel Standard", "Manga B4".
     */
    std::string name;

    Size    canvasSize;   //!< Full canvas dimensions including all margin zones, in pixels.
    Margins margins;      //!< Margin widths on each side, in pixels.

    /**
     * \brief GUI hint: the user entered dimensions as safe-area (content zone) rather
     *        than as absolute canvas size.
     *
     * The stored \c canvasSize is always the absolute canvas (safe area + all margins),
     * regardless of this flag.  The GUI uses this hint to restore the radio-button
     * state when re-opening the profile editor, so the user sees their data in the
     * same form they entered it.  The library itself never reads this field.
     */
    bool hintUserSafeAreaSelect = false;

    /**
     * \brief Overlay colour used by TemplateGenerator to highlight margin zones.
     *
     * Typically a semi-transparent pink/magenta, e.g. {255, 105, 180, 128}.
     */
    RGBA visualColour = {255, 105, 180, 128};

    /**
     * \brief Background fill colour for the template canvas.
     *
     * Defaults to fully transparent so the artist can layer the template PNG
     * over an existing canvas in any blending mode.  Set to opaque white
     * (\c {255,255,255,255}) when the template should stand alone.
     */
    RGBA backgroundColour = {0, 0, 0, 0};

    /**
     * \brief Bookkeeping for the template PNG generated from this profile.
     *
     * An empty \c templateInfo.path means no template has been generated yet.
     * Not used by the library itself — maintained by the GUI/CLI layer.
     */
    CanvasTemplateInfo templateInfo;

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
