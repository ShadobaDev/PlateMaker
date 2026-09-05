/**
 * \file lib/include/platemaker/models/output_presets.hpp
 * \brief The built-in output-profile catalogue - one definition per publishing platform.
 *
 * Split out of \c output_profile.hpp, which had grown to describe two different things: what an
 * output profile *is*, and which ones ship with the library.  This is the second.
 *
 * A preset is an ordinary \c OutputProfile the library defines in code - there is no "is a preset"
 * field.  Preset-ness is *provenance*: a profile is a preset because it comes from the table below.
 *
 * The single source of truth is a **compile-time table** (\c k_outputPresetDefs): trivial fields plus
 * \c string_view id/name over string literals, so the whole array is \c constexpr.  Full
 * \c OutputProfile objects (which own \c std::string) are materialised from a definition only when a
 * caller needs one; a membership test needs none.  All comparisons are by value, so even if the table
 * is duplicated between the DLL and the exe on MinGW, nothing relies on its address.
 *
 * Presets are never serialised: \c WorkspaceSerializer strips any that reach \c outputProfiles and
 * drops preset copies on load.  They are identical in every build, so a \c ProjectItem::outputProfileId
 * can reference one by its stable id, which \c resolveOutputProfile() resolves against this catalogue.
 * A user who wants to change a preset duplicates it into an ordinary profile.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_OUTPUT_PRESETS_HPP
#define PLATEMAKER_MODELS_OUTPUT_PRESETS_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Models {

/**
 * \brief Compile-time definition of one output preset — the source a preset is materialised from.
 *
 * Every field is a literal type (string_view over a string literal; ints, enums, and aggregates of
 * those), so an array of these is \c constexpr.
 */
struct OutputPresetDef {
    std::string_view id;
    std::string_view name;
    int              targetWidth;
    int              sliceHeight;
    LastSlicePolicy  lastSlicePolicy;
    OutputFormat     outputFormat;
    JpegOptions      jpegOptions;
    PngOptions       pngOptions;
    WebpOptions      webpOptions;
    int              startIndex;
};

//! Canonical ids of the built-in presets. Stable across builds and sessions so a stored
//! ProjectItem::outputProfileId can reference one; resolved from the catalogue, never persisted.
inline constexpr std::string_view k_webtoonStandardPresetId = "op-preset-webtoon-standard";
inline constexpr std::string_view k_tapasPresetId           = "op-preset-tapas";
inline constexpr std::string_view k_namicomiPresetId        = "op-preset-namicomi";
inline constexpr std::string_view k_globalComixPresetId     = "op-preset-globalcomix";
inline constexpr std::string_view k_popjoyPresetId          = "op-preset-popjoy";
inline constexpr std::string_view k_comicFuryPresetId       = "op-preset-comicfury";

//! The preset catalogue — the single, compile-time source of truth. Add a preset by adding a row.
//!
//! Design rule: a preset must ALWAYS produce uploadable files for its platform. So slice heights are
//! kept conservative (≈1.4–3.3 MP) and lossy/compressed formats are preferred — never the largest
//! setting a platform tolerates. Users who want to push size/quality do it in a custom profile. Most
//! presets output JPEG (best compression at visually identical quality); WebP where the platform
//! promotes it; PNG only where the per-chapter limit is so large that lossless is free (NAMICOMI).
inline constexpr std::array<OutputPresetDef, 6> k_outputPresetDefs = {{
    { k_webtoonStandardPresetId, "Webtoon Standard", 800, 1280,
      LastSlicePolicy::KeepAsIs, OutputFormat::JPEG,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
    // Tapas requires 940px width (any height) with a 2 MB per-file limit. Slice height is 1504px so the
    // slice keeps Webtoon's 800:1280 (1:1.6) aspect ratio (940 × 1.6 = 1504); a q90 JPEG at this size
    // stays comfortably under 2 MB.
    { k_tapasPresetId, "Tapas", 940, 1504,
      LastSlicePolicy::KeepAsIs, OutputFormat::JPEG,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
    // NAMICOMI recommends 1200 × 1600 pages and allows a huge 250 MB per-chapter budget, so lossless
    // PNG is free here — the platform's creators prefer it. This is the one preset that is not lossy.
    { k_namicomiPresetId, "NAMICOMI", 1200, 1600,
      LastSlicePolicy::KeepAsIs, OutputFormat::PNG,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
    // GlobalComix promotes HD (its reader upscales well on 4K/tablets) and fully supports WebP. WebP
    // keeps a tall HD slice (1280 × 2560, 3.3 MP) safely uploadable — smaller than the equivalent JPEG.
    { k_globalComixPresetId, "GlobalComix (HD)", 1280, 2560,
      LastSlicePolicy::KeepAsIs, OutputFormat::WebP,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
    // Popjoy targets high-DPI phone screens. 1000 × 2000 (2 MP) q90 JPEG stays well under a typical
    // per-file limit while still giving crisp output on dense displays.
    { k_popjoyPresetId, "Popjoy", 1000, 2000,
      LastSlicePolicy::KeepAsIs, OutputFormat::JPEG,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
    // ComicFury / indie-web CMS: a layout-safe 950px width (won't overflow classic page templates).
    // Slice at 1500px (Google's alternative to publishing whole uncut pages); 1.4 MP q90 JPEG is tiny.
    { k_comicFuryPresetId, "ComicFury / Indie", 950, 1500,
      LastSlicePolicy::KeepAsIs, OutputFormat::JPEG,
      JpegOptions{90, JpegSubsampling::YUV_444, true, false},
      PngOptions{6, false},
      WebpOptions{80, false, 4},
      1 },
}};

//! Materialises a full OutputProfile from a compile-time definition.
[[nodiscard]] inline OutputProfile toOutputProfile(const OutputPresetDef& d)
{
    OutputProfile p;
    p.id              = std::string(d.id);
    p.name            = std::string(d.name);
    p.targetWidth     = d.targetWidth;
    p.sliceHeight     = d.sliceHeight;
    p.lastSlicePolicy = d.lastSlicePolicy;
    p.outputFormat    = d.outputFormat;
    p.jpegOptions     = d.jpegOptions;
    p.pngOptions      = d.pngOptions;
    p.webpOptions     = d.webpOptions;
    p.startIndex      = d.startIndex;
    return p;
}

/**
 * \brief The preset definition with \p id, or \c nullptr — the membership test for preset-ness.
 *
 * The zero-copy discriminator for a bare id: it scans the compile-time table and constructs no
 * OutputProfile. Callers that only ask "is this a preset?" (the GUI's read-only guard, the
 * serializer's write guard) use this; callers that need the object materialise it (below).
 */
[[nodiscard]] inline const OutputPresetDef* outputPresetDefById(std::string_view id)
{
    for (const auto& d : k_outputPresetDefs)
        if (d.id == id) return &d;
    return nullptr;
}

//! Index of the Webtoon Standard preset within k_outputPresetDefs (pinned by the static_assert).
inline constexpr std::size_t k_webtoonStandardPresetIndex = 0;
static_assert(k_outputPresetDefs[k_webtoonStandardPresetIndex].id == k_webtoonStandardPresetId,
              "k_webtoonStandardPresetIndex must point at the Webtoon Standard row");

//! The Webtoon Standard preset as a full OutputProfile (materialised from the catalogue).
[[nodiscard]] inline OutputProfile webtoonStandardPreset()
{
    return toOutputProfile(k_outputPresetDefs[k_webtoonStandardPresetIndex]);
}

/**
 * \brief Every preset this build ships, materialised — for listing and for the GUI's merged view.
 *
 * Built once into a function-local static and returned by const reference, so repeated calls do not
 * re-materialise the vector. Safe as an inline function despite the earlier by-value note: the table is
 * rebuilt from code on every app start (never persisted, so no drift across app updates), and callers
 * only read fields / compare ids by value — nothing relies on the static's address, so a possible
 * per-module duplicate on MinGW is harmless.
 */
[[nodiscard]] inline const std::vector<OutputProfile>& outputProfilePresets()
{
    static const std::vector<OutputProfile> cache = [] {
        std::vector<OutputProfile> out;
        out.reserve(k_outputPresetDefs.size());
        for (const auto& d : k_outputPresetDefs)
            out.push_back(toOutputProfile(d));
        return out;
    }();
    return cache;
}

/**
 * \brief The preset named by \p id as a full OutputProfile, if \p id is one.
 *
 * The materialising counterpart to outputPresetDefById() — one copy, for a caller that needs the whole
 * object (render, or comparing against a stored profile). Prefer outputPresetDefById() for a pure
 * membership check.
 */
[[nodiscard]] inline std::optional<OutputProfile> outputProfilePresetById(std::string_view id)
{
    if (const auto* d = outputPresetDefById(id))
        return toOutputProfile(*d);
    return std::nullopt;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_OUTPUT_PRESETS_HPP
