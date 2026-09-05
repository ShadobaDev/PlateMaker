/**
 * \file lib/src/infrastructure/thumbnail_cache/thumbnail_cache.cpp
 * \brief ThumbnailCache implementation — on-disk 200 px-wide preview cache.
 *
 * Thumbnails are stored in the configured \c .platemaker-cache/ directory and
 * are named by a hex digest of the source file path so that renames produce
 * orphan entries rather than stale hits.  Because the digest is path-only, a file
 * overwritten in place (each re-render rewrites the same \c output_00N slice) keeps
 * its digest; \c getOrGenerate() therefore also checks the cached thumbnail's mtime
 * against the source and regenerates when the source is newer, so a stale preview
 * (the old black-band slice) is never served.  The mtime check catches source *content*
 * changes but not a change in this code's generation *logic* — a \c ".vN" token in the
 * filename (see \c kThumbnailCacheVersion) covers that, orphaning old thumbnails on a bump.
 *
 * This implementation has zero Qt dependency.  The GUI layer is responsible for
 * running \c getOrGenerate() on a background thread (e.g. via \c QtConcurrent::run())
 * when non-blocking behaviour is needed.
 *
 * \note Stage 1 uses std::hash<std::string> for the path digest.
 *       TODO Stage 2: replace with SHA-256 of the source file path for
 *       collision-resistance as specified.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>

#include <vips/vips.h>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Platemaker::Infrastructure {

namespace {

// ---------------------------------------------------------------------------
// Cache-generation version — folded into every thumbnail filename (thumbnailPath).
//
// The cache is keyed on the source *path* and invalidated by mtime only, so it can detect a change in the
// source file's *content* but NOT a change in this code's generation *logic*: a source dated in the past
// (e.g. a 2016 camera JPEG) always has an older mtime than its cached thumbnail, so the thumbnail is judged
// fresh forever. Bumping this constant changes the filename, orphaning every prior thumbnail so it is
// regenerated with current logic.
//
// Bump whenever the pixels generate() produces for the same source could differ — rotation, target size,
// output format, colour handling. History:
//   v1  pre-0.5.0: vips_thumbnail with "no_rotate" (EXIF orientation ignored — sideways previews).
//   v2  0.5.0:     auto-rotate (dropped "no_rotate"); a rotated photo now previews in display orientation.
constexpr int kThumbnailCacheVersion = 2;

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

// A cached thumbnail is only usable if it is at least as new as its source. The cache filename is
// keyed on the source *path* alone, so a file that is overwritten in place (every re-render rewrites
// the same output_00N slice) keeps its digest — and a pure existence check would then serve the
// previous render's thumbnail forever (the black-band slice was the visible symptom). Comparing
// mtimes invalidates that without accumulating orphans: generate() rewrites the same digest file.
// If either timestamp cannot be read, treat the cache as stale and regenerate (safe default).
[[nodiscard]] bool thumbnailIsFresh(const std::string& sourceFilePath,
                                    const std::string& thumbnailFilePath)
{
    namespace fs = std::filesystem;
    std::error_code ecSrc, ecThumb;
    const auto srcTime   = fs::last_write_time(utf8ToPath(sourceFilePath),    ecSrc);
    const auto thumbTime = fs::last_write_time(utf8ToPath(thumbnailFilePath), ecThumb);
    if (ecSrc || ecThumb) return false;
    return thumbTime >= srcTime;
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
    fs::create_directories(utf8ToPath(cacheDirectory), ec);
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
    // Both directions go through the UTF-8 helpers: building the path from a narrow string
    // and reading it back with .string() are the two halves of the same encoding hazard, and
    // .string() additionally throws on MSVC for a character the ANSI page cannot represent.
    // The ".vN" token re-keys the whole cache when generation logic changes (see kThumbnailCacheVersion);
    // it is a filename suffix, not part of the digest, so digest↔path rename semantics stay intact.
    const fs::path name = utf8ToPath(
        pathDigest(sourceFilePath) + ".v" + std::to_string(kThumbnailCacheVersion) + ".png");
    return pathToUtf8(utf8ToPath(m_cacheDirectory) / name);
}

// ---------------------------------------------------------------------------
// isCached — existence check without generation
// ---------------------------------------------------------------------------

bool ThumbnailCache::isCached(const std::string& sourceFilePath) const
{
    namespace fs = std::filesystem;
    const std::string path = thumbnailPath(sourceFilePath);
    std::error_code ec;
    const fs::path fsPath = utf8ToPath(path);
    const auto st = fs::status(fsPath, ec);
    if (ec || !fs::is_regular_file(st)) return false;
    const auto sz = fs::file_size(fsPath, ec);
    return !ec && sz > 0;
}

// ---------------------------------------------------------------------------
// getOrGenerate — synchronous (available in all builds)
// ---------------------------------------------------------------------------

std::string ThumbnailCache::getOrGenerate(const std::string& sourceFilePath)
{
    const std::string thumb = thumbnailPath(sourceFilePath);
    // Existence alone is not enough: the digest is path-only, so an in-place overwrite (a re-render
    // rewriting the same output slice) reuses the digest. Also require the cached thumbnail to be at
    // least as new as the source, or a stale preview (e.g. the old black-band slice) would persist.
    if (isCached(sourceFilePath) && thumbnailIsFresh(sourceFilePath, thumb)) {
        return thumb;
    }
    return generateByDecoding(sourceFilePath);
}

// ---------------------------------------------------------------------------
// generate — shrink a source to a 200 px thumbnail and publish it atomically.
// Two overloads share the shrink params and the write; only the shrink *source*
// differs: a file (vips_thumbnail) or an in-RAM image (vips_thumbnail_image).
// Both auto-rotate by default (no "no_rotate"), so a preview matches the render's
// display orientation; a no-op for untagged images (rendered slices, PNG exports).
// This auto-rotate switch is what kThumbnailCacheVersion==2 records: the bump invalidates the
// pre-0.5.0 sideways thumbnails so they regenerate upright here.
// ---------------------------------------------------------------------------

namespace {

// Consume an already-shrunk thumbnail image (takes ownership) and write it to destPath **atomically**:
// pngsave to a temp sibling, then rename over destPath. Atomicity keeps a concurrent reader from ever
// seeing a partially-written thumbnail — the same guarantee ImageIO::encode gives outputs.
void writeThumbnail(const std::string& destPath, VipsImage* shrunk)
{
    namespace fs = std::filesystem;
    Core::PixelBuffer buf{shrunk}; // takes ownership; frees on scope exit

    const std::string tmpPath = destPath + ".pmtmp";
    if (vips_pngsave(buf.vipsImage(), tmpPath.c_str(), nullptr) != 0) {
        const std::string err = vips_error_buffer();
        std::error_code rmEc; fs::remove(utf8ToPath(tmpPath), rmEc);
        throw std::runtime_error(
            "ThumbnailCache — failed to write thumbnail '" + destPath + "': " + err);
    }
    std::error_code ec;
    fs::rename(utf8ToPath(tmpPath), utf8ToPath(destPath), ec);
    if (ec) {
        std::error_code rmEc; fs::remove(utf8ToPath(tmpPath), rmEc);
        throw std::runtime_error(
            "ThumbnailCache — could not publish thumbnail '" + destPath + "': " + ec.message());
    }
}

} // anonymous namespace

std::string ThumbnailCache::generateByDecoding(const std::string& sourceFilePath)
{
    const std::string destPath = thumbnailPath(sourceFilePath);

    VipsImage* out = nullptr;
    if (vips_thumbnail(sourceFilePath.c_str(), &out, 200,
            "size",      VIPS_SIZE_DOWN,
            nullptr) != 0)
    {
        throw std::runtime_error(
            "ThumbnailCache::generateByDecoding() — vips_thumbnail failed for '" +
            sourceFilePath + "': " + vips_error_buffer());
    }
    writeThumbnail(destPath, out);
    return destPath;
}

std::string ThumbnailCache::generateFromImage(const std::string& sourceFilePath, const Core::PixelBuffer& image)
{
    if (!image.isValid()) {
        throw std::runtime_error(
            "ThumbnailCache::generateFromImage() — in-RAM source image is empty for '" + sourceFilePath + "'");
    }
    const std::string destPath = thumbnailPath(sourceFilePath);

    VipsImage* out = nullptr;
    if (vips_thumbnail_image(image.vipsImage(), &out, 200,
            "size",      VIPS_SIZE_DOWN,
            nullptr) != 0)
    {
        throw std::runtime_error(
            "ThumbnailCache::generate() — vips_thumbnail_image failed for '" +
            sourceFilePath + "': " + vips_error_buffer());
    }
    writeThumbnail(destPath, out);
    return destPath;
}

} // namespace Platemaker::Infrastructure
