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
// A preset is an ordinary OutputProfile that libplatemaker defines in code and ships baked into the
// build — not a distinct type, and carrying no "is a preset" field.  Preset-ness is *provenance*: a
// profile is a preset exactly when it comes from this catalogue.  A consumer listing profiles knows
// that from which vector it asked (outputProfilePresets() vs Workspace::outputProfiles); for a bare
// id, outputProfilePresetById() answers it.
//
// Presets are never serialised (WorkspaceSerializer strips any that reach outputProfiles, and drops
// preset copies on load).  They are identical in every build so a ProjectItem::outputProfileId can
// reference one by its stable id, which resolveOutputProfile() resolves against this catalogue at
// runtime.  Because the catalogue is rebuilt from code on every call, a preset's definition cannot be
// mutated at runtime — the source is the single source of truth; a user who wants to change one
// duplicates it into an ordinary profile.
//
// These live in the model header as inline free functions next to outputProfileSignature():
// OutputProfile is header-only and not PLATEMAKER_EXPORT, so keeping the catalogue inline adds nothing
// to the DLL boundary.
// ---------------------------------------------------------------------------

//! Canonical id of the Webtoon Standard preset.  Stable across builds and sessions so a stored
//! ProjectItem::outputProfileId can reference it; resolved from the catalogue, never persisted.
inline constexpr std::string_view k_webtoonStandardPresetId = "op-preset-webtoon-standard";

/**
 * \brief The Webtoon Standard preset: 800 px wide, 1280 px slices, PNG.
 *
 * Every field is set **explicitly** rather than left to the struct's defaults. The defaults
 * happen to match today, which is precisely the hazard: changing one would silently
 * redefine the preset and desynchronise it from every workspace already on disk.
 */
[[nodiscard]] inline OutputProfile webtoonStandardPreset()
{
    OutputProfile p;
    p.id                       = std::string{k_webtoonStandardPresetId};
    p.name                     = "Webtoon Standard";
    p.targetWidth              = 800;
    p.sliceHeight              = 1280;
    p.lastSlicePolicy          = LastSlicePolicy::KeepAsIs;
    p.outputFormat             = OutputFormat::PNG;
    p.startIndex               = 1;
    p.jpegOptions.quality      = 90;
    p.jpegOptions.subsampling  = JpegSubsampling::YUV_444;
    p.jpegOptions.optimize     = true;
    p.jpegOptions.progressive  = false;
    p.pngOptions.compression   = 6;
    p.pngOptions.interlaced    = false;
    p.webpOptions.quality      = 80;
    p.webpOptions.lossless     = false;
    p.webpOptions.effort       = 4;
    return p;
}

/**
 * \brief Every preset this build ships — the lookup table.
 *
 * The single source of truth for seeding a new workspace, for guaranteeing presets are
 * present when one is loaded, and for marking them in the GUI.
 *
 * \note Returned **by value**, not as a reference to a function-local static: in an inline
 *       function such an object can be duplicated between the DLL and the executable on
 *       MinGW, which would quietly break identity comparisons. With a handful of presets the
 *       copy costs nothing and the hazard disappears.
 */
[[nodiscard]] inline std::vector<OutputProfile> outputProfilePresets()
{
    return { webtoonStandardPreset() };
}

/**
 * \brief The preset named by \p id, if \p id is one — the membership test that decides preset-ness.
 *
 * The discriminator for a bare id (a consumer holding a whole vector already knows preset-ness from
 * its provenance). Returns the canonical preset so the caller can also render or compare against it:
 * the GUI uses it to disable edit/remove on a preset row, and the serializer uses it to keep presets
 * out of persisted output profiles.
 */
[[nodiscard]] inline std::optional<OutputProfile> outputProfilePresetById(std::string_view id)
{
    for (auto& preset : outputProfilePresets())
        if (preset.id == id) return preset;
    return std::nullopt;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
