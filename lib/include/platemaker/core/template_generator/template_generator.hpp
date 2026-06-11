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
 * the supplied CanvasProfile.  It renders:
 * - A white background fill.
 * - A semi-transparent overlay on each margin zone, using CanvasProfile::visualColour.
 * - A solid border around the safe area boundary.
 * - Horizontal slice-cut guide lines at every \c OutputProfile::sliceHeight pixels
 *   (scaled by the ratio of canvasWidth / targetWidth so that the lines appear at
 *   the correct positions on the un-scaled canvas).
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
     * The canvas dimensions and margin zones are taken from \p canvasProfile.
     * The slice-guide line positions are computed from \p outputProfile.sliceHeight
     * scaled to the canvas coordinate space.
     *
     * \param canvasProfile The canvas profile defining canvas size, margins, and overlay colour.
     * \param outputProfile The output profile supplying the target width and slice height used
     *                      to compute guide-line positions on the canvas.
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
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_TEMPLATE_GENERATOR_HPP
