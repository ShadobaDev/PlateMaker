/**
 * \file lib/include/platemaker/models/processing_steps.hpp
 * \brief Optional, render-time processing steps — colour correction and text/bubble overlays.
 *
 * Both features are *optional* stages the render pipeline applies at two distinct injection seams,
 * in two different coordinate spaces (see \c ProcessingStepDomain):
 *  - **Colour correction** — a per-page point grade (ICC → sRGB, brightness/contrast/saturation),
 *    applied in the *page domain* (before a page is scaled and appended to the strip), with optional
 *    per-page exclusions.  Point operations are position-independent, so a project-wide grade applied
 *    per page is pixel-identical to grading the whole strip while keeping exclusions and per-source ICC.
 *  - **Strip overlays** — text/bubbles composited in the *strip domain* (per output slice, at strip-Y).
 *    The library is format-agnostic: it composites a pre-rendered RGBA bitmap supplied by the consumer;
 *    whether that layer came from raster art, an SVG, or laid-out text is the consumer's concern.
 *    Placement is stored **relative to an input page** (\c StripOverlay::anchorInputUid) and resolved
 *    to strip-Y at render time by \c resolveOverlayAnchors(), so editing the chapter moves a bubble
 *    with its own artwork instead of leaving it stranded where the strip used to be.
 *
 * Both are **opt-in and default-off**, so a project that uses neither renders byte-identically to one
 * built before these types existed — the additive JSON codec (guarded reads) preserves old workspaces.
 *
 * The compile-time \c k_processingStepDefs table is the enumerable, GUI-facing description of the step
 * kinds the library understands (a Fusion-like stack renders from it).  Adding a future step is: a new
 * config struct here + a stateless Core applier + one row in this table + a staleness contribution —
 * with no change to the existing steps.  This mirrors the \c k_outputPresetDefs catalogue pattern.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROCESSING_STEPS_HPP
#define PLATEMAKER_MODELS_PROCESSING_STEPS_HPP

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Step configuration structs
// ---------------------------------------------------------------------------

/**
 * \brief One control point of a tone curve, in normalised [0,1] input/output.
 */
struct CurvePoint {
    double x = 0.0; //!< Input value, 0..1.
    double y = 0.0; //!< Output value, 0..1.
};

/**
 * \brief Tone curves: a master (all-channels) curve plus optional per-channel curves.
 *
 * Each curve is a list of control points the renderer interpolates (linearly, MVP) into a lookup
 * table.  An empty curve is the identity.  The master curve applies first; a per-channel curve then
 * maps the master-adjusted value for its channel — so the effective map is
 * \c channelCurve(masterCurve(v)).  Points may be given in any order (the renderer sorts by x).
 */
struct ColourCurves {
    std::vector<CurvePoint> master; //!< Applied to all channels first (empty = identity).
    std::vector<CurvePoint> r;      //!< Red channel, applied after master (empty = identity).
    std::vector<CurvePoint> g;      //!< Green channel.
    std::vector<CurvePoint> b;      //!< Blue channel.
};

//! True when any of the four curves carries control points (i.e. is not the identity).
[[nodiscard]] inline bool hasAnyCurve(const ColourCurves& c)
{
    return !c.master.empty() || !c.r.empty() || !c.g.empty() || !c.b.empty();
}

/**
 * \brief Project-wide colour correction applied per input page at render time (page domain).
 *
 * Non-destructive: the source files are never modified — the grade is applied to a copy in the
 * pipeline.  \c enabled gates the whole step; with it \c false the pipeline does no colour work and
 * the output is byte-identical to a build without this feature.
 *
 * Apply order within the grade: tone curves → brightness/contrast → saturation.
 */
struct ColourCorrection {
    bool enabled = false; //!< Master toggle. When false the step is skipped entirely.

    ColourCurves curves;     //!< Per-channel tone curves (empty = identity). Applied first, 8-bit only (MVP).
    double brightness = 0.0; //!< Additive lift, roughly [-1, 1]; 0 = no change.
    double contrast   = 1.0; //!< Multiplicative contrast around mid-grey; 1 = no change.
    double saturation = 1.0; //!< Chroma scale; 1 = no change, 0 = greyscale.

    /**
     * \brief Input \c uid values this grade skips (e.g. a title or end page).
     *
     * Keyed by \c InputFile::uid (not path) so a rename does not silently un-exclude a page.  An
     * excluded page is rendered exactly as it would be with the whole step disabled.
     */
    std::vector<std::string> excludedInputUids;
};

/**
 * \brief How an overlay is blended onto the slice beneath it — a curated subset of libvips' blend modes.
 *
 * \c Over is normal source-over (the default). The others are the painting modes useful for
 * bubbles/effects; the compositor maps each to the matching \c VipsBlendMode.
 */
enum class BlendMode {
    Over,     //!< Normal source-over (default).
    Multiply, //!< Darkens: result = base × overlay.
    Screen,   //!< Lightens: inverse-multiply.
    Overlay,  //!< Multiply/screen by base lightness (contrast).
    Darken,   //!< Per-channel minimum.
    Lighten   //!< Per-channel maximum.
};

/**
 * \brief One text/bubble overlay composited onto the strip at render time (strip domain).
 *
 * The overlay is a consumer-rendered RGBA bitmap positioned by its top-left corner in **strip
 * coordinates** (the continuous, post-scale strip the slices are cut from).  The compositor draws it
 * onto every output slice its box intersects; libvips clips a layer that straddles a slice cut, so an
 * overlay spanning two slices lands correctly on both.
 *
 * Treated as a **resource parallel to an input file**: the consumer creates the bitmap, but the library
 * owns the inventory — \c ProjectItem::addOverlay() mints the \c uid, computes the \c sha256, and dedups
 * identical content. Prefer that over constructing a record by hand (see \c ProjectItem).
 */
struct StripOverlay {
    std::string uid;        //!< Local unique id (e.g. "ovl-<hex>"), minted by ProjectItem::addOverlay().
    std::string bitmapPath; //!< Absolute path to the pre-rendered RGBA layer on disk.
    std::string sha256;     //!< SHA-256 of the bitmap — feeds staleness + dedup (a re-rendered layer re-renders output).

    /**
     * \brief The input page this overlay rides on — empty means absolute strip coordinates.
     *
     * Keyed by \c InputFile::uid, the same stable page identity \c ColourCorrection::excludedInputUids
     * uses, so a rename does not detach a bubble from its page.  When set, \c y is measured from that
     * page's top edge in the strip rather than from the strip's; \c resolveOverlayAnchors() turns the
     * pair back into an absolute strip-Y once the layout is known.
     *
     * This is what survives editing the chapter.  A bubble stored at an absolute strip-Y drifts onto the
     * wrong artwork the moment anything above it changes height — a page inserted or reordered, a canvas
     * profile's margins edited, a page dropped as unreadable — and the drift is silent.  Anchored, the
     * bubble moves with its page and only its own page can move it.
     */
    std::string anchorInputUid;

    int       x = 0;                    //!< Top-left X (pixels). Every page is scaled to the same target
                                        //!< width, so the strip and a page share one X origin — \c x means
                                        //!< the same thing anchored or not.
    int       y = 0;                    //!< Top-left Y (pixels): from the anchor page's top when
                                        //!< \c anchorInputUid is set, else from the strip's top.
    bool      enabled = true;           //!< Per-overlay toggle; a disabled overlay is not composited.
    BlendMode blend   = BlendMode::Over; //!< How it blends onto the slice beneath.
};

/**
 * \brief Turns page-anchored overlays into absolute strip coordinates for a known strip layout.
 *
 * The bridge between the two ways an overlay can be placed (see \c StripOverlay::anchorInputUid): the
 * durable, page-relative form the project stores, and the absolute strip-Y the compositor draws at.
 * Both the render and a consumer's preview call this with the layout they are about to draw, so the
 * preview cannot disagree with the render about where a bubble lands.
 *
 * An unanchored overlay passes through untouched.  An anchored one gets its page's top added to \c y and
 * comes back unanchored — the result is uniformly absolute, so calling this twice is harmless.
 *
 * An overlay whose anchor page is **not in the layout** is *dropped from the result* and reported in
 * \p orphanedUids.  That is the honest reading of "the page it sat on is not being rendered": the page
 * may have been removed, or skipped this run as missing/unreadable.  Nothing is deleted — the record
 * stays in the project, so a consumer can list orphans and offer to re-anchor them, and the overlay
 * reappears by itself once its page is back.
 *
 * \param overlays          The project's overlays, in composite order.
 * \param pageTopByInputUid Strip-Y of each page's top edge, keyed by \c InputFile::uid — built from the
 *                          pages that actually landed in the strip (or, in a consumer, from the same
 *                          preview layout it is drawing).
 * \param orphanedUids      Optional: receives the \c uid of each overlay dropped for a missing anchor.
 * \return The overlays that can be placed, in the input order, all in absolute strip coordinates.
 */
[[nodiscard]] inline std::vector<StripOverlay> resolveOverlayAnchors(
    const std::vector<StripOverlay>&            overlays,
    const std::unordered_map<std::string, int>& pageTopByInputUid,
    std::vector<std::string>*                   orphanedUids = nullptr)
{
    std::vector<StripOverlay> resolved;
    resolved.reserve(overlays.size());

    for (const auto& o : overlays) {
        if (o.anchorInputUid.empty()) {   // already absolute
            resolved.push_back(o);
            continue;
        }

        const auto it = pageTopByInputUid.find(o.anchorInputUid);
        if (it == pageTopByInputUid.end()) {
            if (orphanedUids)
                orphanedUids->push_back(o.uid);
            continue;
        }

        StripOverlay abs = o;
        abs.y += it->second;
        abs.anchorInputUid.clear();
        resolved.push_back(std::move(abs));
    }

    return resolved;
}

// ---------------------------------------------------------------------------
// Step descriptor catalogue (compile-time; GUI-facing enumeration)
// ---------------------------------------------------------------------------

/// The kind of optional processing step — one per built-in step the library understands.
enum class ProcessingStepKind {
    ColourCorrection, //!< Per-page point grade + ICC. See \c ColourCorrection.
    StripOverlay      //!< Text/bubble RGBA overlays. See \c StripOverlay.
};

/// Which pipeline seam a step runs at — its coordinate space (see the file header).
enum class ProcessingStepDomain {
    Page, //!< Per source page, before scale/append (page/scaled-page coordinates).
    Strip //!< Per output slice, at strip-Y (continuous-strip coordinates).
};

/**
 * \brief Compile-time description of one step kind — the enumerable contract a GUI renders a step
 *        stack/graph from, and the single place a new step's identity/metadata is declared.
 *
 * Every field is a literal type (string_view over a string literal, enums, bool), so an array of
 * these is \c constexpr — the same zero-cost catalogue shape as \c OutputPresetDef.
 */
struct ProcessingStepDef {
    std::string_view     id;             //!< Stable identifier, safe to persist / reference across builds.
    std::string_view     name;           //!< Human-readable label for the GUI.
    ProcessingStepKind   kind;           //!< Which built-in step this describes.
    ProcessingStepDomain domain;         //!< The seam it runs at.
    bool                 defaultEnabled; //!< Whether the step is on by default (both are opt-in: false).
};

//! Canonical ids of the built-in steps. Stable across builds so a stored reference stays valid.
inline constexpr std::string_view k_colourCorrectionStepId = "step-colour-correction";
inline constexpr std::string_view k_stripOverlayStepId     = "step-strip-overlay";

//! The step catalogue — the single, compile-time source of truth. Add a step by adding a row.
inline constexpr std::array<ProcessingStepDef, 2> k_processingStepDefs = {{
    { k_colourCorrectionStepId, "Colour correction",
      ProcessingStepKind::ColourCorrection, ProcessingStepDomain::Page,  false },
    { k_stripOverlayStepId, "Text & bubbles",
      ProcessingStepKind::StripOverlay,     ProcessingStepDomain::Strip, false },
}};

//! The step definition with \p id, or \c nullptr — the membership test for a step id.
[[nodiscard]] inline const ProcessingStepDef* processingStepDefById(std::string_view id)
{
    for (const auto& d : k_processingStepDefs)
        if (d.id == id) return &d;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Staleness signature
// ---------------------------------------------------------------------------

/**
 * \brief A deterministic fingerprint of the processing config that changes output bytes.
 *
 * Empty when nothing would alter the render (colour correction disabled *and* no enabled overlay), so a
 * project using neither has an empty signature — the same value a workspace saved before these steps
 * existed carries, so comparing against it never forces a needless re-render.  Any output-affecting
 * change — enabling/adjusting the grade, a page exclusion, an overlay's content (sha256), position or
 * enabled state — changes the string, so a caller folds a mismatch into its "config changed → full
 * re-render" decision exactly like \c outputProfileSignature().
 *
 * Overlays are emitted in composite order (their z-order affects the output); excluded input uids are
 * sorted (their order does not change the result, so it must not change the signature).
 *
 * An overlay contributes its **stored** placement — anchor uid plus the offset — not the strip-Y it
 * resolves to, because the signature is computed from the project alone, before any layout exists. Two
 * placements that happen to resolve to the same strip-Y therefore differ here; that errs towards
 * re-rendering, which is the safe direction. Where an anchored overlay actually lands also depends on
 * the pages above it, and a change to those is already a change to the input set that forces a
 * re-render on its own axis.
 */
[[nodiscard]] inline std::string processingConfigSignature(
    const ColourCorrection& cc, const std::vector<StripOverlay>& overlays)
{
    using std::to_string;
    const auto curveSig = [](const std::vector<CurvePoint>& pts) {
        std::string cs;
        for (const auto& p : pts)
            cs += "(" + std::to_string(p.x) + "," + std::to_string(p.y) + ")";
        return cs;
    };

    std::string s;

    if (cc.enabled) {
        s += "cc{cu m" + curveSig(cc.curves.master) + "r" + curveSig(cc.curves.r)
           + "g" + curveSig(cc.curves.g) + "b" + curveSig(cc.curves.b)
           + ";b" + to_string(cc.brightness)
           + ";c" + to_string(cc.contrast)
           + ";s" + to_string(cc.saturation) + ";x";
        std::vector<std::string> excluded = cc.excludedInputUids;
        std::sort(excluded.begin(), excluded.end());
        for (const auto& uid : excluded)
            s += uid + ",";
        s += "}";
    }

    for (const auto& o : overlays) {
        if (!o.enabled)
            continue;
        s += "ov{" + o.sha256 + ";" + o.anchorInputUid + "@" + to_string(o.x) + "," + to_string(o.y)
           + ";" + to_string(static_cast<int>(o.blend)) + "}";
    }

    return s;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROCESSING_STEPS_HPP
