/**
 * \file
 * \brief PageItem data model — represents a single source image file in the workspace.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_PAGE_ITEM_HPP
#define PLATEMAKER_MODELS_PAGE_ITEM_HPP

#include <string>

#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/**
 * \class PageItem
 * \brief Metadata record for one source image file participating in the strip pipeline.
 *
 * PageItem does **not** hold pixel data and does **not** hold a thumbnail path.
 * Thumbnails are a GUI concern: the Qt layer asks \c ThumbnailCache::getOrGenerate()
 * on demand when it needs to display a preview.  The thumbnail path is deterministic
 * (derived from \c filePath) so it never needs to be persisted in the workspace.
 *
 * \note The \c id field is a UUID string generated once at creation time and never
 *       changed thereafter — it is the stable identity key for this page across
 *       renames or moves.
 */
class PageItem {
public:
    std::string id;        //!< UUID v4 string — stable identity key for this page.
    std::string filePath;  //!< Absolute path to the source image file on disk.
    int         order = 0; //!< 0-based display order within the strip (not filesystem order).

    /**
     * \brief Current processing status of this page.
     *
     * Set by the pipeline after each run.  Persisted in the workspace so the
     * GUI can show which files have been processed since the last session.
     */
    PageStatus status = PageStatus::Pending;

    /**
     * \brief Human-readable error or skip reason.
     *
     * Non-empty only when \c status is \c PageStatus::Error or \c PageStatus::Skipped.
     */
    std::string errorMessage;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PAGE_ITEM_HPP
