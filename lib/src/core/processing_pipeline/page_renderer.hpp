/**
 * \file lib/src/core/processing_pipeline/page_renderer.hpp
 * \brief PageRenderer — the page domain: one input page, from file to its slot on the strip.
 *
 * Internal to the pipeline: this header lives under \c src/ and is never installed.  What it
 * describes, though, is the single most load-bearing rule in the library — **there is exactly one
 * definition of what a page looks like on the strip**, and all three public entry points
 * (\c render(), \c layoutPagesFromHeaders(), \c decodePageToRgba()) go through it.  A viewer that re-derived
 * any of this would drift from the render *silently*: both would work, the numbers would just stop
 * agreeing, and every page below the first disagreement would sit at the wrong strip offset.
 *
 * Split in two deliberately.  \c planFromHeader() makes every *decision* about a page — its
 * EXIF-upright display size, which canvas profile applies, what to report about that — from the
 * header alone, touching no pixels.  \c scaledPage() turns a plan into pixels.  The split is what
 * lets \c layoutPagesFromHeaders() price a whole chapter at a header read per page.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_PAGE_RENDERER_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_PAGE_RENDERER_HPP

#include <string>
#include <vector>

#include <platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp>
#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/processing_steps.hpp>

namespace Platemaker::Core {

/**
 * \brief Image geometry read from the header only — no pixel decode.
 *
 * \c width / \c height are the *display* dimensions and are -1 on error.  For EXIF Orientation
 * 5–8 (the 90°/270° cases) the stored width and height are transposed, so this reports the size
 * the autorotated scaler actually produces — which is what keeps canvas-profile matching in
 * agreement with what the render builds.
 */
struct HeaderGeometry {
    int  width          = -1;
    int  height         = -1;
    int  orientation    = 1;      //!< EXIF Orientation (1 = normal); 1 when the tag is absent.
    bool hasOrientation = false;  //!< Whether the file actually carried an Orientation tag.
};

/**
 * \brief Header-only decisions about one input page. Producing this touches no pixels.
 */
struct PagePlan {
    HeaderGeometry               geo;                //!< Display size + EXIF orientation, from the header.
    const Models::CanvasProfile* profile = nullptr;  //!< Matched profile; null → render implicitly, no margins.
    InputStatus                  status  = InputStatus::Appended; //!< What render() reports for this page.
    std::vector<std::string>     candidateIds;       //!< Same-size workspace profiles not linked to the project.
    std::string                  candidateName;      //!< The first of those — for the diagnostic message only.
};

/**
 * \class PageRenderer
 * \brief Resolves and renders one input page, in the page domain (before the strip).
 *
 * Holds only what every page in a run shares — the profile matcher, whether the project uses
 * canvas profiles at all, and the target width — so a caller constructs one per run and asks it
 * about each page in turn.
 *
 * \note Borrows \p matcher by reference; it must outlive the renderer.  Both entry points are
 *       \c const, so one renderer is safe to reuse across every page of a run.
 */
class PageRenderer {
public:
    /**
     * \brief Binds to the matcher and output width every page in this run shares.
     *
     * \param matcher      Resolves a page's display size to a canvas profile. Must outlive this.
     * \param hasProfiles  Whether the workspace palette is non-empty.  Carried separately because
     *                     an empty palette is not merely "nothing matches": it selects the plain
     *                     scaled pipeline and skips the dimension check entirely.
     * \param targetWidth  The width every page is scaled to — the output profile's.
     */
    PageRenderer(const CanvasProfileMatcher& matcher, bool hasProfiles, int targetWidth) noexcept
        : m_matcher(matcher), m_hasProfiles(hasProfiles), m_targetWidth(targetWidth) {}

    /**
     * \brief Resolves what will happen to \p filePath, from its header alone.
     *
     * \param filePath Absolute path of the input page.
     * \return The plan: display geometry, matched profile (or none), and the status to report.
     * \throws std::runtime_error if the project has canvas profiles but the file's dimensions
     *         cannot be read — matching cannot proceed without a size.
     */
    [[nodiscard]] PagePlan planFromHeader(const std::string& filePath) const;

    /**
     * \brief Turns a plan into the scaled page the strip receives: load → [grade] → [crop] → scale.
     *
     * The grade never changes how the file is *read* — only what happens to the pixels afterwards
     * — so enabling the colour step with neutral values is a true no-op on every path.
     *
     * Every libvips operation this builds is lazy: it settles the output dimensions on
     * construction and reads the file's header, but decodes nothing until a consumer pulls a
     * pixel.  That is what lets \c layoutPagesFromHeaders() call this purely to read dimensions off the
     * result and still pay only for a header read.
     *
     * \param plan     The plan from \c planFromHeader().
     * \param filePath Absolute path of the input page (the same one the plan describes).
     * \param grade    Non-null to bake the grade into the returned pixels; null for an ungraded
     *                 page.  The render bakes it in; the preview does not, because its consumer
     *                 re-grades the returned pixels on every slider move and must not pay for a
     *                 re-decode to do it.
     * \return The scaled page, ready to append to the strip.
     */
    [[nodiscard]] ScaledImage scaledPage(const PagePlan&                 plan,
                                         const std::string&              filePath,
                                         const Models::ColourCorrection* grade) const;

    //! True when the workspace has canvas profiles at all — decides whether match reporting applies.
    [[nodiscard]] bool usesCanvasProfiles() const noexcept { return m_hasProfiles; }

private:
    const CanvasProfileMatcher& m_matcher;
    bool                        m_hasProfiles;
    int                         m_targetWidth;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_PAGE_RENDERER_HPP
