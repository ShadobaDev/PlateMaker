/**
 * \file lib/include/platemaker/models/output_profile.hpp
 * \brief OutputProfile and JpegOptions data models — describe how processed output is generated.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
#define PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/**
 * \brief JPEG encoding parameters used when OutputProfile::outputFormat is OutputFormat::JPEG.
 *
 * All fields carry sensible defaults that produce high-quality, well-optimised
 * JPEG files suitable for online publication.
 */
struct JpegOptions {
    int             quality     = 90;                     //!< Encoding quality, 1–95 (95 = best quality, largest file).
    JpegSubsampling subsampling = JpegSubsampling::YUV_444; //!< Chroma subsampling mode.
    bool            optimize    = true;                   //!< Enable Huffman table optimisation (slightly slower, smaller files).
    bool            progressive = false;                  //!< Write progressive (multi-scan) JPEG instead of baseline.
};

/**
 * \brief PNG encoding parameters used when OutputProfile::outputFormat is OutputFormat::PNG.
 */
struct PngOptions {
    int  compression = 6;     //!< zlib compression level, 0–9 (higher = smaller, slower).
    bool interlaced  = false; //!< Write an Adam7-interlaced (progressive) PNG.
};

/**
 * \brief WebP encoding parameters used when OutputProfile::outputFormat is OutputFormat::WebP.
 */
struct WebpOptions {
    int  quality  = 80;    //!< Encoding quality, 0–100 (ignored when \c lossless).
    bool lossless = false; //!< Encode losslessly instead of lossy.
    int  effort   = 4;     //!< Compression effort, 0–6 (higher = smaller, slower).
};

/**
 * \class OutputProfile
 * \brief A named output configuration that controls how the virtual strip is
 *        scaled, sliced, and saved.
 *
 * Multiple profiles can coexist inside a workspace (e.g. "Webtoon Standard",
 * "Webtoon HD", "Instagram Square").  Per-project assignment is done via
 * \c ProjectItem::outputProfileId.
 */
class OutputProfile {
public:
    std::string id;   //!< Stable unique identifier — never changes after creation.

    /**
     * \brief Human-readable profile name shown in the GUI and used for workspace lookup.
     *
     * Example: "Webtoon Standard".
     */
    std::string name;

    int              targetWidth     = 800;                       //!< Target width in pixels after scaling each source image (Webtoon default: 800).
    int              sliceHeight     = 1280;                      //!< Output slice height in pixels (Webtoon default: 1280).
    LastSlicePolicy  lastSlicePolicy = LastSlicePolicy::KeepAsIs; //!< How the final (potentially shorter) tail slice is handled.
    OutputFormat     outputFormat    = OutputFormat::PNG;         //!< Container format for all output slice files.
    JpegOptions      jpegOptions;                                 //!< JPEG-specific encoding parameters (used when outputFormat == JPEG).
    PngOptions       pngOptions;                                  //!< PNG-specific encoding parameters (used when outputFormat == PNG).
    WebpOptions      webpOptions;                                 //!< WebP-specific encoding parameters (used when outputFormat == WebP).
    int              startIndex      = 1;                         //!< First output file number, e.g. 1 → output_001.png, 5 → output_005.png.
};

/**
 * \brief Returns the lowercase file extension (including the dot) that \p fmt
 *        produces, e.g. ".png", ".jpg", ".webp".
 */
[[nodiscard]] inline std::string outputFormatExtension(OutputFormat fmt)
{
    switch (fmt) {
        case OutputFormat::PNG:  return ".png";
        case OutputFormat::JPEG: return ".jpg";
        case OutputFormat::WebP: return ".webp";
    }
    return ".png";
}

/**
 * \brief Returns a string that uniquely fingerprints every \c OutputProfile field
 *        that affects the produced slice files (names *and* bytes).
 *
 * Used to detect output-invalidating configuration changes: if the signature
 * stored on a \c ProjectItem at render time differs from the current profile's
 * signature, the existing outputs are stale and a full re-render is required
 * (e.g. switching PNG→JPEG, changing target width / slice height / quality).
 *
 * \note Does not yet include linked canvas-profile margins (separate follow-up).
 */
[[nodiscard]] inline std::string outputProfileSignature(const OutputProfile& p)
{
    using std::to_string;
    return "w" + to_string(p.targetWidth) +
           "h" + to_string(p.sliceHeight) +
           "p" + to_string(static_cast<int>(p.lastSlicePolicy)) +
           "f" + to_string(static_cast<int>(p.outputFormat)) +
           "i" + to_string(p.startIndex) +
           // All format options are folded in so toggling back and forth is detected.
           ";jpeg" + to_string(p.jpegOptions.quality) +
           "," + to_string(static_cast<int>(p.jpegOptions.subsampling)) +
           "," + to_string(p.jpegOptions.optimize) +
           "," + to_string(p.jpegOptions.progressive) +
           ";png" + to_string(p.pngOptions.compression) +
           "," + to_string(p.pngOptions.interlaced) +
           ";webp" + to_string(p.webpOptions.quality) +
           "," + to_string(p.webpOptions.lossless) +
           "," + to_string(p.webpOptions.effort);
}

// ---------------------------------------------------------------------------
// Presets
//
// A preset is an ordinary OutputProfile that libplatemaker defines in code — not a distinct type, and
// carrying no "is a preset" field. Preset-ness is *provenance*: a profile is a preset exactly when it
// comes from the catalogue below. A consumer holding a whole vector knows that from which one it asked
// (outputProfilePresets() vs Workspace::outputProfiles); for a bare id, outputPresetDefById() answers
// it with no OutputProfile constructed at all.
//
// The single source of truth is a **compile-time table** of definitions (k_outputPresetDefs): trivial
// fields plus string_view id/name over string literals, so the whole array is constexpr. Full
// OutputProfile objects (which own std::string) are *materialised* from a definition only when a caller
// needs the object; the membership test needs none. All comparisons are by value (string_view ==), so
// even if the constexpr table is duplicated between the DLL and the exe on MinGW nothing relies on its
// address — the table stays header-only with no DLL-boundary concern.
//
// Presets are never serialised (WorkspaceSerializer strips any that reach outputProfiles, and drops
// preset copies on load). They are identical in every build, so a ProjectItem::outputProfileId can
// reference one by its stable id, which resolveOutputProfile() resolves against this catalogue. A user
// who wants to change a preset duplicates it into an ordinary profile.
// ---------------------------------------------------------------------------

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

//! Canonical id of the Webtoon Standard preset. Stable across builds and sessions so a stored
//! ProjectItem::outputProfileId can reference it; resolved from the catalogue, never persisted.
inline constexpr std::string_view k_webtoonStandardPresetId = "op-preset-webtoon-standard";

//! The preset catalogue — the single, compile-time source of truth. Add a preset by adding a row.
inline constexpr std::array<OutputPresetDef, 1> k_outputPresetDefs = {{
    { k_webtoonStandardPresetId, "Webtoon Standard", 800, 1280,
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

#endif // PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
