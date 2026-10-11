/**
 * \file lib/include/platemaker/core/template_generator/template_generator.hpp
 * \brief TemplateGenerator — renders a canvas template (PNG or PSD) for use as a Procreate guide layer.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
#define PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP

#include <string>

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
     * \throws std::invalid_argument if the canvas size or margins are degenerate, the dpi is not positive,
     *                               or a PSD side exceeds 30000 px.
     * \throws std::runtime_error    if the image cannot be written to \p outputPath.
     */
    void generate(
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
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
