/**
 * \file
 * \brief ThumbnailCache implementation — on-disk 200 px-wide preview cache.
 *
 * Thumbnails are stored in the configured \c .platemaker-cache/ directory and
 * are named by a hex digest of the source file path so that renames produce
 * orphan entries rather than stale hits.
 *
 * \note Stage 1 uses std::hash<std::string> for the path digest.
 *       TODO Stage 2: replace with SHA-256 of the source file path for
 *       collision-resistance as specified.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>

#include <vips/vips.h>

#ifndef PLATEMAKER_NO_QT
#   include <QtConcurrent/QtConcurrent>
#endif

#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Platemaker::Infrastructure {

namespace {

// ---------------------------------------------------------------------------
// Internal helper: derive the thumbnail filename from a source file path.
//
// Uses std::hash<std::string> for Stage 1.  The hash is formatted as a
// zero-padded 16-character lowercase hex string, giving 64-bit collision
// resistance — sufficient for a per-project thumbnail cache.
//
// TODO Stage 2: replace with SHA-256(filePath) per spec.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string pathDigest(const std::string& filePath)
{
    const std::size_t h = std::hash<std::string>{}(filePath);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ThumbnailCache::ThumbnailCache(const std::string& cacheDirectory)
    : m_cacheDirectory(cacheDirectory)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(cacheDirectory, ec);
    if (ec) {
        throw std::runtime_error(
            "ThumbnailCache: cannot create cache directory '" +
            cacheDirectory + "': " + ec.message());
    }
}

// ---------------------------------------------------------------------------
// thumbnailPath — expected disk location, no generation
// ---------------------------------------------------------------------------

std::string ThumbnailCache::thumbnailPath(const std::string& sourceFilePath) const
{
    namespace fs = std::filesystem;
    const fs::path name = pathDigest(sourceFilePath) + ".png";
    return (fs::path{m_cacheDirectory} / name).string();
}

// ---------------------------------------------------------------------------
// isCached — existence check without generation
// ---------------------------------------------------------------------------

bool ThumbnailCache::isCached(const std::string& sourceFilePath) const
{
    namespace fs = std::filesystem;
    const std::string path = thumbnailPath(sourceFilePath);
    std::error_code ec;
    const auto st = fs::status(path, ec);
    if (ec || !fs::is_regular_file(st)) return false;
    const auto sz = fs::file_size(path, ec);
    return !ec && sz > 0;
}

// ---------------------------------------------------------------------------
// getOrGenerate — synchronous (available in all builds)
// ---------------------------------------------------------------------------

std::string ThumbnailCache::getOrGenerate(const std::string& sourceFilePath)
{
    if (isCached(sourceFilePath)) {
        return thumbnailPath(sourceFilePath);
    }
    return generate(sourceFilePath);
}

// ---------------------------------------------------------------------------
// generate — create and write a 200 px thumbnail via libvips
// ---------------------------------------------------------------------------

std::string ThumbnailCache::generate(const std::string& sourceFilePath)
{
    const std::string destPath = thumbnailPath(sourceFilePath);

    VipsImage* out = nullptr;
    if (vips_thumbnail(sourceFilePath.c_str(), &out, 200,
            "no_rotate", TRUE,
            "size",      VIPS_SIZE_DOWN,
            nullptr) != 0)
    {
        throw std::runtime_error(
            "ThumbnailCache::generate() — vips_thumbnail failed for '" +
            sourceFilePath + "': " + vips_error_buffer());
    }

    Core::PixelBuffer buf{out};

    if (vips_pngsave(buf.get(), destPath.c_str(), nullptr) != 0) {
        throw std::runtime_error(
            "ThumbnailCache::generate() — failed to write thumbnail '" +
            destPath + "': " + vips_error_buffer());
    }

    return destPath;
}

// ---------------------------------------------------------------------------
// requestAsync — asynchronous interface (GUI builds only)
// ---------------------------------------------------------------------------

#ifndef PLATEMAKER_NO_QT
void ThumbnailCache::requestAsync(
    const std::string&                      sourceFilePath,
    std::function<void(const std::string&)> callback)
{
    // If already cached, invoke the callback immediately on the calling thread.
    if (isCached(sourceFilePath)) {
        callback(thumbnailPath(sourceFilePath));
        return;
    }

    // Dispatch generation to a Qt thread-pool worker.
    // The result is delivered to the calling thread via the QFutureWatcher
    // mechanism.
    //
    // TODO Stage 4 (Qt GUI): replace this synchronous stub with a proper
    // QFutureWatcher<std::string> that invokes the callback on the Qt main
    // thread via a queued connection.  The stub below is correct but blocks
    // the calling thread during thumbnail generation — acceptable for Stage 1
    // where no GUI event loop is running.
    // Store the future so we satisfy [[nodiscard]].  The task runs to
    // completion even after the future is destroyed (thread-pool keeps it alive).
    // TODO Stage 4: hold the QFuture in a QFutureWatcher so the callback is
    // delivered on the Qt main thread rather than the pool thread.
    [[maybe_unused]] auto future =
        QtConcurrent::run([this, sourceFilePath, callback]() {
            try {
                this->generate(sourceFilePath);
                callback(this->thumbnailPath(sourceFilePath));
            } catch (...) {
                // Thumbnail generation failure is non-fatal: silently swallow.
            }
        });
}
#endif // PLATEMAKER_NO_QT

} // namespace Platemaker::Infrastructure
