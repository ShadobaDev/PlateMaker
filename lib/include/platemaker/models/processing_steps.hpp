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

#include "platemaker/models/colour_correction.hpp"
#include "platemaker/models/strip_overlay.hpp"

namespace Platemaker::Models {

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
        s += "cc{cu m" + curveSig(cc.curves.master) + "r" + curveSig(cc.curves.red)
           + "g" + curveSig(cc.curves.green) + "b" + curveSig(cc.curves.blue)
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
