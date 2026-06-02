/**
 * \file lib/include/platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp
 * \brief ThumbnailCache — generates and caches 200 px-wide preview images on disk.
 *
 * This class is part of \c libplatemaker and has zero Qt dependency.
 * It provides only synchronous, blocking methods.  The GUI layer is responsible
 * for wrapping calls in \c QtConcurrent::run() when asynchronous behaviour is
 * required — \c libplatemaker does not make that policy decision.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_THUMBNAIL_CACHE_HPP
#define PLATEMAKER_INFRASTRUCTURE_THUMBNAIL_CACHE_HPP

#include <string>

namespace Platemaker::Infrastructure {

/**
 * \class ThumbnailCache
 * \brief On-disk cache of 200 px-wide thumbnail images derived from source files.
 *
 * Thumbnails are stored in a \c .platemaker-cache/ directory placed alongside the
 * \c .platemaker.json workspace file.  The cache directory is auto-created on first
 * use and is safe to delete at any time — thumbnails are regenerated transparently
 * on next access.
 *
 * Each thumbnail is named by a digest of the source file path so that renames
 * do not produce stale entries (the old entry is simply orphaned and ignored until
 * the cache is cleaned).
 *
 * \note \b Usage \b policy: The CLI binary never calls this class — thumbnails are a
 *       pure GUI concern (file list widget, hover previews).  The Qt GUI calls
 *       \c getOrGenerate() on demand when a \c PageItem needs to be displayed.
 *       For non-blocking behaviour the GUI wraps the call in \c QtConcurrent::run();
 *       \c libplatemaker itself does not depend on Qt and makes no threading decisions.
 */
class ThumbnailCache {
public:
    /**
     * \brief Constructs a ThumbnailCache rooted at the given directory.
     *
     * The directory is created if it does not exist.
     *
     * \param cacheDirectory Absolute path to the \c .platemaker-cache/ directory.
     * \throws std::runtime_error if the directory cannot be created.
     */
    explicit ThumbnailCache(const std::string& cacheDirectory);

    /**
     * \brief Returns the path to a valid thumbnail for \p sourceFilePath.
     *
     * If a thumbnail already exists on disk it is returned immediately.  Otherwise
     * the thumbnail is generated synchronously (blocking) before returning.
     *
     * \param sourceFilePath Absolute path to the source image file.
     * \return Absolute path to the cached thumbnail PNG file.
     *
     * \throws std::runtime_error if thumbnail generation fails.
     */
    [[nodiscard]] std::string getOrGenerate(const std::string& sourceFilePath);

    /**
     * \brief Returns the expected thumbnail path without generating it.
     *
     * Useful for checking cache existence before deciding whether to request
     * generation.
     *
     * \param sourceFilePath Absolute path to the source image file.
     * \return Absolute path where the thumbnail would be stored.
     */
    [[nodiscard]] std::string thumbnailPath(const std::string& sourceFilePath) const;

    /**
     * \brief Returns \c true if a cached thumbnail for \p sourceFilePath already exists on disk.
     *
     * \param sourceFilePath Absolute path to the source image file.
     * \return \c true if the thumbnail PNG exists and is non-empty.
     */
    [[nodiscard]] bool isCached(const std::string& sourceFilePath) const;

private:
    std::string m_cacheDirectory; //!< Root directory where thumbnails are stored.

    /**
     * \brief Generates a thumbnail for the given source file and writes it to disk.
     *
     * \param sourceFilePath Absolute path to the source image file.
     * \return Absolute path to the generated thumbnail file.
     *
     * \throws std::runtime_error if generation fails.
     */
    std::string generate(const std::string& sourceFilePath);
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_THUMBNAIL_CACHE_HPP
