/**
 * \file lib/src/core/processing_pipeline/page_renderer.cpp
 * \brief PageRenderer implementation — the page domain, lifted verbatim from the pipeline.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include "page_renderer.hpp"

#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/log/log.hpp>

#include <stdexcept>

#include <vips/vips.h>

namespace Platemaker::Core {

namespace {

namespace Log = Platemaker::Infrastructure::Log;

/**
 * \brief Reads a file's display dimensions and EXIF Orientation without decoding any pixels.
 *
 * \param filePath Absolute path of the image.
 * \return The geometry; \c width / \c height are -1 when the file cannot be opened.
 */
HeaderGeometry headerGeometry(const std::string& filePath)
{
    HeaderGeometry geo;
    VipsImage* img = vips_image_new_from_file(
        filePath.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return geo;
    }
    geo.width  = img->Xsize;
    geo.height = img->Ysize;
    if (vips_image_get_typeof(img, VIPS_META_ORIENTATION) != 0) {
        int o = 0;
        if (vips_image_get_int(img, VIPS_META_ORIENTATION, &o) == 0) {
            geo.orientation    = o;
            geo.hasOrientation = true;
        }
    }
    // Orientation 5–8 rotate by 90°/270°, transposing the displayed image. Report the display size
    // (what the autorotated scaler produces) so matching and rendering agree; 1–4 keep the stored
    // dimensions.
    if (geo.orientation >= 5 && geo.orientation <= 8) {
        const int t = geo.width; geo.width = geo.height; geo.height = t;
    }
    g_object_unref(img);
    return geo;
}

} // namespace

PagePlan PageRenderer::planFromHeader(const std::string& filePath) const
{
    PagePlan plan;
    plan.geo = headerGeometry(filePath);

    PLATEMAKER_LOG(Log::ProcessingPipeline,
            "read " + filePath + ": header "
            + std::to_string(plan.geo.width) + "x" + std::to_string(plan.geo.height)
            + ", EXIF orientation "
            + (plan.geo.hasOrientation ? std::to_string(plan.geo.orientation)
                                       : std::string("none")));

    // No canvas profiles at all → the standard pipeline: scaled, never cropped. The dimension check
    // below is deliberately skipped in that case (as it always has been): nothing needs the size, and
    // a genuinely unreadable file fails loudly in the scaler instead.
    if (!m_hasProfiles) return plan;

    if (plan.geo.width <= 0 || plan.geo.height <= 0)
        throw std::runtime_error("cannot determine image dimensions");

    const ProfileMatchResult result = m_matcher.resolve(plan.geo.width, plan.geo.height);
    if (result.status == ProfileMatchResult::Status::Matched) {
        plan.profile = result.profile;
    } else if (result.status == ProfileMatchResult::Status::FoundInWorkspaceOnly) {
        // A same-size profile exists in the workspace but is not linked to this project. Render
        // implicitly (no margins) rather than drop the page — linking the profile is a one-click fix.
        plan.status = InputStatus::AppendedProfileNotLinked;
        plan.candidateIds.reserve(result.workspaceCandidates.size());
        for (const auto* cand : result.workspaceCandidates)
            plan.candidateIds.push_back(cand->id);
        if (!result.workspaceCandidates.empty())
            plan.candidateName = result.workspaceCandidates.front()->name;
    } else {
        // No profile of this size exists anywhere — render the page implicitly, exactly the
        // no-profiles path. Determinism is preserved by visibility (the input is flagged), not omission.
        plan.status = InputStatus::AppendedWithoutProfile;
    }
    return plan;
}

ScaledImage PageRenderer::scaledPage(const PagePlan&                 plan,
                                     const std::string&              filePath,
                                     const Models::ColourCorrection* grade) const
{
    // All four are stateless and empty — constructing them per page costs nothing and keeps the page
    // domain self-contained.
    Scaler                  scaler;
    MarginCropper           cropper;
    Infrastructure::ImageIO imageIO;
    ColourCorrector         colourCorrector;

    const bool doMarginCrop =
        plan.profile != nullptr &&
        (plan.profile->margins.top    > 0 ||
         plan.profile->margins.bottom > 0 ||
         plan.profile->margins.left   > 0 ||
         plan.profile->margins.right  > 0);

    if (doMarginCrop) {
        // The margin path has always normalised to sRGB on load (a no-op for a file with no embedded
        // profile, which is the common case) — keep it, graded or not.
        auto buf = imageIO.load(filePath, /*convertToSRGB=*/true);
        if (grade)
            buf = colourCorrector.apply(std::move(buf), *grade);
        auto cropped = cropper.crop(buf, plan.profile->margins);
        return scaler.scale(std::move(cropped), filePath, m_targetWidth);
    }
    if (grade) {
        // Load explicitly so the grade can sit between load and scale. `false` because the un-graded
        // version of this path goes straight through Scaler, which does not transform — same pixels in,
        // whether or not a grade follows.
        auto buf = imageIO.load(filePath, /*convertToSRGB=*/false);
        buf = colourCorrector.apply(std::move(buf), *grade);
        return scaler.scale(std::move(buf), filePath, m_targetWidth);
    }
    return scaler.scale(filePath, m_targetWidth);
}

} // namespace Platemaker::Core
