/**
 * \file lib/src/core/processing_pipeline/strip_builder.cpp
 * \brief StripBuilder implementation — the strip-assembly phase, lifted from the pipeline.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include "strip_builder.hpp"

#include "pipeline_log.hpp"

#include <platemaker/models/canvas_profile.hpp>

#include <utility>

namespace Platemaker::Core {

using Platemaker::Models::FileStatus;
using Platemaker::Models::ProcessingErrorCategory;
using Platemaker::Models::ProcessingErrorCode;

StripBuilder::StripBuilder(const PageRenderer&             pages,
                           const Models::ColourCorrection& colourCorrection,
                           const ProcessingCallbacks&      callbacks)
    : m_pages(pages)
    , m_colourCorrection(colourCorrection)
    , m_callbacks(callbacks)
    , m_gradeExcluded(colourCorrection.excludedInputUids.begin(),
                      colourCorrection.excludedInputUids.end())
{
}

bool StripBuilder::appendAllPages(const std::vector<Models::InputFile>&    inputs,
                                  const Infrastructure::CancellationToken& cancel,
                                  ProcessingOutcome&                       outcome)
{
    for (const auto& file : inputs) {
        if (cancel.isCancelled()) {
            outcome.cancelled = true;
            return false;
        }

        if (file.status == FileStatus::Missing) {
            emitLog(m_callbacks.onLog, ProcessingLogLevel::Warning,
                    "Skipping missing file: " + file.filePath);
            reportSkip(file, InputStatus::SkippedMissing, {}, outcome);
            continue;
        }

        try {
            appendOnePage(file, outcome);
        } catch (const std::exception& e) {
            emitLog(m_callbacks.onLog, ProcessingLogLevel::Warning,
                    std::string("Skipping (") + e.what() + "): " + file.filePath);
            reportSkip(file, InputStatus::SkippedError, e.what(), outcome);
        }
    }
    return true;
}

void StripBuilder::appendOnePage(const Models::InputFile& file, ProcessingOutcome& outcome)
{
    // The header is read unconditionally: the display W×H is recorded per input (below) so a later
    // run can tell offline which canvas profile each page would match now — matching is purely by
    // W×H, and without the stored dimensions any profile-list change degrades to a whole-project
    // re-render. A header read is negligible beside the decode + scale that follow.
    PagePlan plan = m_pages.planFromHeader(file.filePath);

    reportProfileMatch(file, plan);

    // Record what was actually applied to this page. Editing a profile leaves both the input and
    // the output file byte-identical, so this is the only trace that lets the next run notice the
    // page went stale.
    outcome.appliedProfiles.push_back(Models::AppliedCanvasProfile{
        file.filePath,
        plan.profile ? plan.profile->id : std::string{},
        plan.profile ? Models::canvasRenderFingerprint(*plan.profile)
                     : std::string{},
        plan.geo.width,
        plan.geo.height});

    // Grade this page when the step is enabled and the page is not excluded. When off,
    // scaledPage() runs exactly the historical code paths (byte-identical output).
    const bool applyGrade =
        m_colourCorrection.enabled && m_gradeExcluded.count(file.uid) == 0;

    // Read before the append: appending is what moves the strip's bottom edge, so this is the Y the
    // page starts at.
    const int pageTopY = m_strip.totalHeight();

    m_strip.append(m_pages.scaledPage(plan, file.filePath,
                                      applyGrade ? &m_colourCorrection : nullptr));

    if (!file.uid.empty())
        m_pageTopByUid[file.uid] = pageTopY;

    if (m_callbacks.onInput)
        m_callbacks.onInput({file.filePath, plan.status, std::move(plan.candidateIds), {}});
}

void StripBuilder::reportProfileMatch(const Models::InputFile& file, const PagePlan& plan) const
{
    if (!m_pages.usesCanvasProfiles())
        return;

    const std::string dims = std::to_string(plan.geo.width) + "x"
                           + std::to_string(plan.geo.height);
    if (plan.profile) {
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Info,
                "Applied canvas profile '" + plan.profile->name + "' (" + dims
                + ") to " + file.filePath);
    } else if (plan.status == InputStatus::AppendedProfileNotLinked) {
        // Say it loudly: linking the profile is a one-click fix that would apply its margins.
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Warning,
                "No linked canvas profile matches " + dims + " — rendering " + file.filePath
                + " without margins; profile '" + plan.candidateName + "' (" + dims
                + ") exists in the workspace but is not linked to this project."
                + " Link it to apply its margins.");
    } else {
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Info,
                "No canvas profile matches " + dims + " — rendering " + file.filePath
                + " without margins");
    }
}

void StripBuilder::reportSkip(const Models::InputFile& file, InputStatus status,
                              const std::string& detail, ProcessingOutcome& outcome) const
{
    outcome.skippedPages.push_back(file.filePath);
    if (!m_callbacks.onInput)
        return;

    InputResult r;
    r.inputPath = file.filePath;
    r.status    = status;
    r.detail    = detail;
    if (status == InputStatus::SkippedError) {
        // Non-fatal: reported here, not on the outcome.
        r.errorCode     = ProcessingErrorCode::InputLoadFailed;
        r.errorCategory = ProcessingErrorCategory::Load;
    }
    m_callbacks.onInput(r);
}

} // namespace Platemaker::Core
