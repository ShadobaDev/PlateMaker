/**
 * \file lib/src/core/processing_pipeline/strip_builder.hpp
 * \brief StripBuilder — phase 1: turns the input list into one assembled virtual strip.
 *
 * Internal to the pipeline: this header lives under \c src/ and is never installed.
 *
 * A class rather than a function because the phase carries state that outlives it: the strip
 * itself, and the input-uid → strip-Y map.  That map can only be built here — this loop is the
 * one point in a run where a page's uid and its Y offset are both known — and it is consumed
 * later, when the strip-domain overlays are placed.  Returning both from a free function would
 * mean either an out-parameter pair or a throwaway result struct; owning them is simpler.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_STRIP_BUILDER_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_STRIP_BUILDER_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

#include "page_renderer.hpp"

namespace Platemaker::Core {

/**
 * \class StripBuilder
 * \brief Loads, grades, scales and appends every input page, in strip order.
 *
 * One page failing is not one run failing: a page that cannot be read is reported and skipped,
 * exactly as it always has been, and the run continues with the rest.  A run where *nothing*
 * loaded is the caller's to reject — this class reports what happened and lets \c run() decide.
 */
class StripBuilder {
public:
    /**
     * \brief Binds to the per-run collaborators.
     *
     * \param pages            The page domain. Must outlive this builder.
     * \param colourCorrection The project's grade; \c enabled false → no colour work at all, so
     *                         the historical load/scale paths run untouched.
     * \param callbacks        Progress/log sinks; any field may be null.
     */
    StripBuilder(const PageRenderer&             pages,
                 const Models::ColourCorrection& colourCorrection,
                 const ProcessingCallbacks&      callbacks);

    /**
     * \brief Appends every input in \p inputs to the strip, in the order given.
     *
     * Records one \c AppliedCanvasProfile per appended page on \p outcome, and one entry in
     * \c skippedPages for every input that was missing or failed to load.
     *
     * \param inputs   The project's inputs, already in strip order.
     * \param cancel   Polled once per input; a cancelled run stops where it is.
     * \param outcome  Accumulates applied profiles, skipped pages, and the cancelled flag.
     * \return \c false if cancellation stopped the build (and \c outcome.cancelled is set);
     *         \c true when every input was processed, whether or not some were skipped.
     */
    bool appendAllPages(const std::vector<Models::InputFile>&    inputs,
                        const Infrastructure::CancellationToken& cancel,
                        ProcessingOutcome&                       outcome);

    //! The assembled strip. Non-const because slicing consumes it.
    [[nodiscard]] ScaledStrip& strip() noexcept { return m_strip; }

    /**
     * \brief Where each page's top edge landed, keyed by input uid.
     *
     * Pages that failed to load never get an entry, so an overlay anchored to one is reported as
     * orphaned instead of silently landing on its neighbour.
     */
    [[nodiscard]] const std::unordered_map<std::string, int>& pageTopByInputUid() const noexcept
    {
        return m_pageTopByUid;
    }

private:
    //! Plans, grades and appends one page; records its profile. Throws on any load/scale failure.
    void appendOnePage(const Models::InputFile& file, ProcessingOutcome& outcome);

    //! Reports which canvas profile matched \p file, and how loudly, via the log callback.
    void reportProfileMatch(const Models::InputFile& file, const PagePlan& plan) const;

    //! Reports an input the run could not use, and records it on \p outcome.
    void reportSkip(const Models::InputFile& file, InputStatus status,
                    const std::string& detail, ProcessingOutcome& outcome) const;

    const PageRenderer&             m_pages;
    const Models::ColourCorrection& m_colourCorrection;
    const ProcessingCallbacks&      m_callbacks;

    //! Uids excluded from the grade, hashed once rather than scanned per page.
    std::unordered_set<std::string> m_gradeExcluded;

    ScaledStrip                          m_strip;
    std::unordered_map<std::string, int> m_pageTopByUid;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_STRIP_BUILDER_HPP
