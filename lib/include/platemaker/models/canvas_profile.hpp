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
#include <vector>
#include "platemaker/platemaker_export.h"
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/// The file a template is written as.
enum class TemplateFormat {
    Png, //!< One flat image, to place on a canvas as a reference.
    Psd, //!< A layered document that opens as a canvas of exactly this size, with nothing to place.
};

/**
 * \struct CanvasTemplateInfo
 * \brief Bookkeeping for one template file generated from a CanvasProfile — one per format.
 *
 * Stored inside the workspace (per canvas profile) so a workspace tracks only its
 * own templates.  \c path is kept **relative to the workspace directory** so the
 * workspace folder stays portable.
 *
 * \c fingerprint is the template's signature (see TemplateGenerator::canvasSignature())
 * — the canvas, its resolution and the template settings — captured at generation time;
 * \c sha256 is the hash of the bytes written.  Against the current profile and the file on
 * disk they tell whether the template is still up to date (TemplateGenerator::check()) — the
 * output profile is deliberately not part of a template's identity.
 */
struct CanvasTemplateInfo {
    std::string    path;                          //!< Template path, relative to the workspace dir.
    TemplateFormat format = TemplateFormat::Png;  //!< What the file is; one entry per format.
    std::string    fingerprint;                   //!< Template signature at generation time.
    std::string    sha256;                        //!< Hash of the bytes written; empty = unknown (a migrated entry).
    std::string    generatedAt;                   //!< ISO 8601 timestamp of the last generation.
};

/**
 * \struct CanvasTemplateSettings
 * \brief How this profile's template is written — what the canvas itself does not say.
 *
 * Its resolution is not here: a template is an image of the canvas, so it takes CanvasProfile::dpi.
 */
struct CanvasTemplateSettings {
    TemplateFormat format     = TemplateFormat::Psd; //!< Opens in Procreate as the canvas itself.
    bool           emptyLayer = false;               //!< PSD only: an empty "Layer" above the template.
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
     * \brief The resolution the artist draws at, in dots per inch.
     *
     * Recorded in the template, so a drawing app sizes its canvas physically as intended. The render
     * works in pixels and never reads it.
     */
    double dpi = 300.0;

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
     * \brief Bookkeeping for the templates generated from this profile: at most one per format,
     *        so a PNG and a PSD can live side by side.
     *
     * Empty means no template has been generated yet. Maintained by the GUI/CLI layer; the
     * library reads it to check a template (TemplateGenerator::check()) and to package it.
     */
    std::vector<CanvasTemplateInfo> templates;

    CanvasTemplateSettings templateSettings; //!< How the template is written (format, an empty layer).

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

    /// The tracked template in \p format, or \c nullptr when none was generated in it.
    [[nodiscard]] const CanvasTemplateInfo* templateIn(TemplateFormat format) const noexcept;
};

// ---------------------------------------------------------------------------
// Telling profiles apart
// ---------------------------------------------------------------------------

/// What a profile means besides its name, in the order that best tells two apart.
enum class ProfileField {
    CanvasSize,       //!< canvasSize
    Margins,          //!< margins
    Dpi,              //!< dpi
    TemplateSettings, //!< templateSettings (format, empty layer)
    Colours,          //!< visualColour, backgroundColour
};

/**
 * \brief How a profile stands among others of the same name (names may repeat).
 */
struct ProfileDistinction {
    bool duplicate = false;              //!< Identical to another profile of its name in every field.
    std::vector<ProfileField> differs;   //!< Fields that set it apart from the others of its name, in enum order.
};

/**
 * \brief Compares \p cp with the other profiles in \p palette that share its name.
 *
 * Names compare without case (ASCII), as files named after them do on Windows. "The others" are those
 * with another id; a profile without an id (not saved yet) is compared with all. A unique name gives
 * an empty result. Neither the id, the name nor templates nor hintUserSafeAreaSelect count — only what
 * the profile means (ProfileField).
 */
[[nodiscard]] PLATEMAKER_EXPORT ProfileDistinction distinguishProfile(
    const std::vector<CanvasProfile>& palette, const CanvasProfile& cp);

/// True when \p a and \p b mean the same, whatever their names: equal in every ProfileField.
[[nodiscard]] PLATEMAKER_EXPORT bool sameProfileContent(const CanvasProfile& a, const CanvasProfile& b);

// ---------------------------------------------------------------------------
// Render-relevant identity
// ---------------------------------------------------------------------------

/**
 * \brief Returns the render-relevant identity of \p cp — everything about the profile
 *        that can change output pixels, and nothing else.
 *
 * Covers \c canvasSize (which page a profile matches) and \c margins (what
 * MarginCropper removes).  Recorded per input at render time, then compared on the
 * next run: a mismatch means that page's output is stale.
 *
 * \warning Deliberately **excludes** \c visualColour and \c backgroundColour.  Those
 *          only affect the generated template PNG, never a render — folding them in
 *          would force a full re-render every time the overlay colour is nudged.
 *          This is why \c TemplateGenerator::canvasSignature() (which does include them,
 *          correctly, for template identity) must not be reused here.
 *
 * \note Format matches the house convention (\c outputProfileSignature,
 *       \c TemplateGenerator::canvasSignature): a deterministic, tagged, human-inspectable
 *       string rather than a hash — it stays readable in the workspace JSON and is
 *       shorter than a hex digest.
 *
 * \param cp The profile to fingerprint.
 * \return e.g. \c "cw=1600;ch=10240;mt=100;mr=100;mb=100;ml=100"
 */
[[nodiscard]] PLATEMAKER_EXPORT std::string canvasRenderFingerprint(const CanvasProfile& cp);

// ---------------------------------------------------------------------------
// Dimension match
// ---------------------------------------------------------------------------

/**
 * \brief Returns \c true when \p cp is the canvas for an image of size \p w × \p h.
 *
 * Canvas matching is exact equality on the full \c canvasSize — an image belongs to a
 * profile only when its pixel dimensions are the profile's canvas dimensions.  This is
 * the single definition of that rule, shared by \c CanvasProfileMatcher::resolveForSize() (the
 * render-time resolver) and \c ProjectItem::detectCanvasConfigChange() (the offline
 * staleness re-match), so the two can never drift apart.
 *
 * \param cp Profile to test.
 * \param w  Image width in pixels.
 * \param h  Image height in pixels.
 */
[[nodiscard]] PLATEMAKER_EXPORT bool canvasSizeMatches(const CanvasProfile& cp, int w, int h) noexcept;

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_CANVAS_PROFILE_HPP
