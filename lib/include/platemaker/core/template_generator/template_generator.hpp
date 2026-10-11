/**
 * \file lib/include/platemaker/core/template_generator/template_generator.hpp
 * \brief TemplateGenerator — renders a canvas template (PNG or PSD) for use as a Procreate guide layer.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */


#ifndef PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
#define PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

namespace Platemaker::Core {

using Models::TemplateFormat;

/**
 * \brief How a template file is written, as opposed to what it shows (the canvas profile).
 *
 * A profile stores its own (CanvasProfile::dpi, CanvasProfile::templateSettings); optionsFor() reads
 * them, and a caller overrides a field only for a one-off file (the CLI's flags).
 */
struct TemplateOptions {
    /// Resolution recorded in the file, in dots per inch. Pixels are unaffected; a drawing app reads it
    /// to size the canvas physically, and one that disagrees with it may resample the image on import.
    double dpi = 300.0;
    TemplateFormat format = TemplateFormat::Png;
    /// PSD only: an empty layer named "Layer" above the template, ready to draw on. Ignored for PNG.
    bool emptyLayer = false;
};

/// One field of a template's signature that changed since the template was generated.
struct TemplateDifference {
    std::string field; //!< The signature's tag: cw, ch, mt, mr, mb, ml, vc, bg, dpi, fmt, layer.
    std::string was;   //!< Its value in the template ("" when the template predates the field).
    std::string now;   //!< Its value the profile asks for now.
};

/**
 * \brief Why a tracked template is not up to date — every reason found, none when it is.
 */
struct TemplateCheck {
    bool        fileMissing    = false; //!< The file is not on disk.
    std::string sharedWith;             //!< A different profile (this name), tracking the same file, overwrote it with its own template.
    bool        changedOutside = false; //!< The file's bytes are not the ones written, and no namesake explains it (only known when a hash was recorded).
    std::vector<TemplateDifference> differences; //!< Signature fields that changed, in signature order.

    [[nodiscard]] bool upToDate() const noexcept
    {
        return !fileMissing && sharedWith.empty() && !changedOutside && differences.empty();
    }
};

/**
 * \class TemplateGenerator
 * \brief Generates a canvas template image that an artist opens in Procreate (PSD) or imports into
 *        it as a reference layer (PNG) before drawing.
 *
 * The generated template is a full-resolution image at the canvas size defined by
 * the supplied CanvasProfile.  It currently renders:
 * - A background-colour fill (CanvasProfile::backgroundColour).
 * - A semi-transparent overlay on each margin zone (CanvasProfile::visualColour).
 *
 * A safe-area border and horizontal slice-cut guide lines (the latter positioned from
 * \c OutputProfile::sliceHeight, scaled by canvasWidth / targetWidth) are implemented but
 * **currently compiled out** behind the \c GUIDELINES_ENABLED switch in template_generator.cpp,
 * kept for possible future reuse.  While they are disabled, \p outputProfile is unused.
 *
 * All drawing operations use libvips — there is no Qt dependency.
 *
 * TemplateGenerator is stateless and thread-safe.
 *
 * \note The template is saved losslessly as PNG or PSD (TemplateOptions::format), regardless of the
 *       OutputProfile::outputFormat setting, at the resolution in TemplateOptions::dpi. A PSD holds the
 *       template as a layer named "Template", plus an empty "Layer" above it on request; PSD caps each
 *       side at 30000 px.
 */
class PLATEMAKER_EXPORT TemplateGenerator {
public:
    TemplateGenerator() = default;

    /**
     * \brief Renders and saves a canvas template image to \p outputPath.
     *
     * The canvas dimensions and margin zones are taken from \p canvasProfile.  (The slice-guide
     * lines that \p outputProfile would position are currently compiled out — see the class note.)
     *
     * \param canvasProfile The canvas profile defining canvas size, margins, and overlay colour.
     * \param outputProfile Reserved for the (currently disabled) slice-guide lines — their target
     *                      width and slice height. Ignored while the guides are compiled out.
     * \param outputPath    Absolute path (UTF-8) where the template file will be written, whatever its
     *                      extension. Parent directories must already exist.
     * \param options       How the file is written: resolution, format, an empty layer.
     *
     * \return The SHA-256 (hex) of the bytes written — computed from them in memory, not by reading the
     *         file back, which in a synced folder may still be the old copy. Record it in
     *         CanvasTemplateInfo::sha256 so check() can tell a file changed outside the application.
     *
     * \throws std::invalid_argument if the canvas size or margins are degenerate, the dpi is not positive,
     *                               or a PSD side exceeds 30000 px.
     * \throws std::runtime_error    if the image cannot be written to \p outputPath.
     */
    std::string generate(
        const Models::CanvasProfile& canvasProfile,
        const Models::OutputProfile& outputProfile,
        const std::string&           outputPath,
        const TemplateOptions&       options = {}) const;

    /**
     * \brief The options a profile asks for: its dpi and its template settings.
     */
    [[nodiscard]] static TemplateOptions optionsFor(const Models::CanvasProfile& canvasProfile);

    /**
     * \brief Computes a canvas-only signature identifying a template's content.
     *
     * The signature is a deterministic string built from exactly the fields that
     * define a template: canvas size, margins, the visual and background colours,
     * the resolution recorded in it, and how it is written — the format and, for a
     * PSD, the empty layer.  The output profile is intentionally excluded — slice
     * guide lines are a cosmetic aid, not part of a template's identity.
     *
     * Comparing a stored signature against a freshly computed one tells a caller
     * whether a previously generated template is still up to date.
     *
     * \param canvasProfile The profile whose template identity is hashed.
     * \return A stable, comparable signature string.
     */
    [[nodiscard]] static std::string canvasSignature(
        const Models::CanvasProfile& canvasProfile);

    /**
     * \brief canvasSignature() of \p canvasProfile written as \p format — the signature a template file
     *        of that format should carry, whatever format the profile asks for now.
     */
    [[nodiscard]] static std::string templateSignature(
        const Models::CanvasProfile& canvasProfile, Models::TemplateFormat format);

    /**
     * \brief Checks one tracked template of \p canvasProfile against the profile and the file.
     *
     * \param palette       Every canvas profile of the workspace, to find a different profile tracking
     *                      the same file (an identical one sharing it is fine — the file would be the same).
     * \param canvasProfile The profile \p entry belongs to.
     * \param entry         One of its CanvasProfile::templates.
     * \param absolutePath  \p entry's file, resolved against the workspace folder (UTF-8).
     */
    [[nodiscard]] static TemplateCheck check(const std::vector<Models::CanvasProfile>& palette,
                                             const Models::CanvasProfile&              canvasProfile,
                                             const Models::CanvasTemplateInfo&         entry,
                                             const std::string&                        absolutePath);
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
