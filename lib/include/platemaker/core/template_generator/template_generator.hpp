/**
 * \file lib/include/platemaker/core/template_generator/template_generator.hpp
 * \brief TemplateGenerator — renders a canvas template PNG for use as a Procreate guide layer.
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

/**
 * \class TemplateGenerator
 * \brief Generates a PNG canvas template image that an artist imports into Procreate
 *        as a reference layer before drawing.
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
 * \note The template is always saved as a lossless PNG regardless of the
 *       OutputProfile::outputFormat setting.
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
     * \param outputPath    Absolute path where the PNG template file will be written.
     *                      Parent directories must already exist.
     *
     * \throws std::invalid_argument if the canvas size or margins are degenerate.
     * \throws std::runtime_error    if the image cannot be written to \p outputPath.
     */
    void generate(
        const Models::CanvasProfile& canvasProfile,
        const Models::OutputProfile& outputProfile,
        const std::string&           outputPath) const;

    /**
     * \brief Computes a canvas-only signature identifying a template's content.
     *
     * The signature is a deterministic string built from exactly the fields that
     * define a template's appearance: canvas size, margins, and the visual and
     * background colours.  The output profile is intentionally excluded — slice
     * guide lines are a cosmetic aid, not part of a template's identity.
     *
     * Comparing a stored signature against a freshly computed one tells a caller
     * whether a previously generated template is still up to date.
     *
     * \param canvasProfile The profile whose template identity is hashed.
     * \return A stable, comparable signature string.
     */
    [[nodiscard]] static std::string signature(
        const Models::CanvasProfile& canvasProfile);
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
