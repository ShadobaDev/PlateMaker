/**
 * \file
 * \brief ThumbnailCache — generates and caches 200 px-wide preview images on disk.
 *
 * In GUI builds (PLATEMAKER_NO_QT not defined) thumbnail generation is performed
 * asynchronously using Qt infrastructure injected by the caller.
 * In CLI builds (PLATEMAKER_NO_QT defined) generation is synchronous.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#pragma once

#ifndef PLATEMAKER_INFRASTRUCTURE_THUMBNAIL_CACHE_HPP
#define PLATEMAKER_INFRASTRUCTURE_THUMBNAIL_CACHE_HPP

#include <functional>
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
 * Each thumbnail is named by the SHA-256 of the source file path so that renames
 * do not produce stale entries (the old entry is simply orphaned and ignored until
 * the cache is cleaned).
 *
 * \note This class is the only component in libplatemaker with conditional Qt
 *       behaviour.  When \c PLATEMAKER_NO_QT is defined the async overload is
 *       compiled out; callers should use the synchronous \c getOrGenerate() method.
 */
class ThumbnailCache {
public:
    /**
     * \brief Constructs a ThumbnailCache rooted at the given directory.
     *
     * The directory is created if it does not exist.
     *
     * \param cacheDirectory Absolute path to the \c .platemaker-cache/ directory.
     */
    explicit ThumbnailCache(const std::string& cacheDirectory);

    // ---------------------------------------------------------------------------
    // Synchronous interface (available in all builds)
    // ---------------------------------------------------------------------------

    /**
     * \brief Returns the path to a valid thumbnail for \p sourceFilePath.
     *
     * If a thumbnail already exists on disk it is returned immediately.  Otherwise
     * the thumbnail is generated synchronously before returning.
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
     * async generation.
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

#ifndef PLATEMAKER_NO_QT
    // ---------------------------------------------------------------------------
    // Asynchronous interface (GUI builds only)
    // ---------------------------------------------------------------------------

    /**
     * \brief Requests thumbnail generation in a background thread via QtConcurrent.
     *
     * If the thumbnail already exists the callback is invoked immediately (on the
     * caller's thread) with the cached path.  Otherwise generation is dispatched
     * to a thread pool and the \p callback is invoked on the calling thread via a
     * queued connection when the thumbnail is ready.
     *
     * \note This overload is only compiled when \c PLATEMAKER_NO_QT is not defined.
     *
     * \param sourceFilePath Absolute path to the source image file.
     * \param callback       A callable invoked with the thumbnail path when ready.
     *                       The callback must be safe to call from the Qt main thread.
     */
    void requestAsync(
        const std::string&                     sourceFilePath,
        std::function<void(const std::string&)> callback);
#endif // PLATEMAKER_NO_QT

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
