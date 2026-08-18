/**
 * \file
 * \brief Unit tests for ThumbnailCache freshness (on-disk preview invalidation).
 *
 * The cache filename is a digest of the source *path* only, so a file overwritten in place — which is
 * exactly what a re-render does to each `output_00N` slice — keeps its digest. These pin that a stale
 * preview is never served: getOrGenerate() must regenerate when the source is newer than the cached
 * thumbnail (the black-band-slice regression), yet must still reuse the cache when nothing changed.
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment, so
 * these tests just synthesise small PNGs on disk.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-17
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>

#include <vips/vips.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

namespace Platemaker::Infrastructure {
namespace {

namespace fs = std::filesystem;

/// A temp directory unique to one test, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
        : m_path(fs::temp_directory_path() /
                 ("platemaker-thumb-" + tag + "-" +
                  std::to_string(::testing::UnitTest::GetInstance()->random_seed())))
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(m_path, ec); }

    [[nodiscard]] std::string file(const std::string& name) const { return (m_path / name).string(); }

private:
    fs::path m_path;
};

/// Writes a synthetic black PNG of the given size to `path` (overwriting) and returns it.
std::string writePng(const std::string& path, int w, int h)
{
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, w, h, nullptr), 0) << "vips_black failed: " << vips_error_buffer();
    EXPECT_EQ(vips_image_write_to_file(img, path.c_str(), nullptr), 0)
        << "write failed: " << vips_error_buffer();
    g_object_unref(img);
    return path;
}

/// Pixel dimensions of an image file.
std::pair<int, int> imageSize(const std::string& path)
{
    VipsImage* img = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(img, nullptr) << "open failed: " << vips_error_buffer();
    if (!img) return {-1, -1};
    const std::pair<int, int> dims{img->Xsize, img->Ysize};
    g_object_unref(img);
    return dims;
}

// A thumbnail is capped at 200 px wide and never upscaled (VIPS_SIZE_DOWN), so a landscape source
// yields a short thumbnail and a taller source a taller one — the height is what tells the two apart.

TEST(ThumbnailCacheTest, RegeneratesWhenSourceOverwrittenNewer)
{
    TempDir tmp("regen");
    const std::string cacheDir = tmp.file("cache");
    const std::string src      = tmp.file("output_001.png");

    ThumbnailCache cache(cacheDir);

    writePng(src, 400, 200);                              // landscape 2:1
    const std::string thumb1 = cache.getOrGenerate(src);
    const auto [w1, h1] = imageSize(thumb1);
    // vips_thumbnail fits within a 200x200 box, so a landscape source gives a wider-than-tall preview.
    EXPECT_GT(w1, h1) << "landscape source should give a landscape thumbnail";

    // Overwrite the source in place with the opposite shape, and make it strictly newer than the
    // thumbnail (real renders are seconds/minutes apart; pin it so the test can't race the clock).
    writePng(src, 400, 800);                              // portrait — thumbnail should flip to taller-than-wide
    fs::last_write_time(src, fs::last_write_time(thumb1) + std::chrono::seconds(2));

    const std::string thumb2 = cache.getOrGenerate(src);
    EXPECT_EQ(thumb2, thumb1) << "path-only digest should keep the same cache file";

    const auto [w2, h2] = imageSize(thumb2);
    // A stale cache hit would still return the old landscape thumbnail; the fix regenerates it from the
    // new portrait source, so the preview is now taller than wide (and different from the first).
    EXPECT_NE(std::make_pair(w2, h2), std::make_pair(w1, h1))
        << "thumbnail was not regenerated after the source was overwritten";
    EXPECT_GT(h2, w2) << "regenerated thumbnail should be portrait, matching the new source";
}

TEST(ThumbnailCacheTest, ReusesCacheWhenSourceUnchanged)
{
    TempDir tmp("reuse");
    const std::string cacheDir = tmp.file("cache");
    const std::string src      = tmp.file("output_001.png");

    ThumbnailCache cache(cacheDir);

    writePng(src, 400, 200);
    const std::string thumb1  = cache.getOrGenerate(src);
    const auto        thumbT1 = fs::last_write_time(thumb1);

    // Source untouched (older than the thumbnail): a second call must be a pure cache hit and not
    // rewrite the file — guards against over-invalidation regenerating on every access.
    const std::string thumb2 = cache.getOrGenerate(src);
    EXPECT_EQ(thumb2, thumb1);
    EXPECT_EQ(fs::last_write_time(thumb2), thumbT1) << "an unchanged source should not be regenerated";
}

// --- in-RAM overload: generate(sourceFilePath, PixelBuffer) — the Arch C pre-warm path ---------------

Core::PixelBuffer blackBuffer(int w, int h)
{
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, w, h, nullptr), 0) << vips_error_buffer();
    return Core::PixelBuffer{img};
}

TEST(ThumbnailCacheTest, GenerateFromInRamImageWritesThumbnailUnderTheSourceDigest)
{
    TempDir tmp("inram");
    ThumbnailCache cache(tmp.file("cache"));

    // sourceFilePath is only the cache key; the pixels come from the in-RAM image, so the file need not
    // exist on disk. A landscape 400x200 source shrinks to a 200x100 preview (fits the 200px box).
    const std::string src   = tmp.file("output_001.jpg");
    const std::string thumb = cache.generate(src, blackBuffer(400, 200));

    ASSERT_TRUE(fs::exists(thumb));
    EXPECT_EQ(thumb, cache.thumbnailPath(src)) << "filed under the source path's digest";
    const auto [w, h] = imageSize(thumb);
    EXPECT_LE(w, 200);
    EXPECT_LE(h, 200);
    EXPECT_GT(w, h) << "a landscape source should give a landscape thumbnail";
}

TEST(ThumbnailCacheTest, WarmedInRamThumbnailIsServedWithoutReadingTheSource)
{
    TempDir tmp("warm");
    ThumbnailCache cache(tmp.file("cache"));

    // The Arch C guarantee: after the pipeline warms the cache from the in-RAM slice, the consumer's
    // getOrGenerate(path) is a cache hit that never re-reads the output. Prove it by making the on-disk
    // source LANDSCAPE (400x200) but warming from a PORTRAIT in-RAM image (200x400): a cache hit returns
    // the portrait thumbnail, whereas a re-read of the source would return a landscape one.
    const std::string src = writePng(tmp.file("output_001.png"), 400, 200);
    cache.generate(src, blackBuffer(200, 400));  // warm from RAM (portrait)
    // Make the source strictly older than the warmed thumbnail so the freshness check is a hit.
    fs::last_write_time(src, fs::last_write_time(cache.thumbnailPath(src)) - std::chrono::seconds(2));

    const auto [w, h] = imageSize(cache.getOrGenerate(src));
    EXPECT_GT(h, w) << "getOrGenerate returned the RAM-warmed (portrait) thumbnail — it did not re-open "
                       "the landscape source";
}

} // namespace
} // namespace Platemaker::Infrastructure
