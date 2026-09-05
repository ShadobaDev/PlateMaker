/**
 * \file lib/src/core/processing_pipeline/pipeline_log.hpp
 * \brief emitLog — the pipeline's one place for "tell the caller something, if it is listening".
 *
 * Internal to the pipeline: this header lives under \c src/ and is never installed, so it is not
 * part of the public API.  It exists because all three pipeline phases — the strip build, the
 * slice write, and \c run() itself — report to the same optional \c ProcessingCallbacks::onLog,
 * and writing the null check at every one of those sites would be the same three lines fifteen
 * times over.
 *
 * This is the *user-facing* channel (what the CLI prints, what the GUI shows in its log pane).
 * Per-component developer tracing is a separate channel entirely — \c PLATEMAKER_LOG.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_PIPELINE_LOG_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_PIPELINE_LOG_HPP

#include <functional>
#include <string>

#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>

namespace Platemaker::Core {

/**
 * \brief Reports \p msg to \p onLog at \p level, doing nothing when the caller did not supply one.
 *
 * \param onLog The caller's log sink; a null \c std::function means "not interested".
 * \param level Severity of the line.
 * \param msg   The message, already formatted.
 */
inline void emitLog(const std::function<void(ProcessingLogLevel, const std::string&)>& onLog,
                    ProcessingLogLevel level, const std::string& msg)
{
    if (onLog) onLog(level, msg);
}

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_PIPELINE_LOG_HPP
