/**
 * \file
 * \brief Reproduces (and pins the fix for) the output-file read/write race behind the render's
 *        "unable to open for write" failures.
 *
 * The GUI writes each slice with ImageIO::encode() on the render worker thread, then — via the
 * onSliceSaved callback — reads that same file back on a QtConcurrent thread to build its thumbnail
 * (ThumbnailCache::generate → vips_thumbnail). These two lib calls therefore touch the same path from
 * two threads. On Windows a reader holding the file makes an *in-place* open-for-write fail. These
 * tests drive exactly those two lib primitives concurrently, with no GUI and no antivirus in the mix:
 *
 *   - InPlaceWrite…      raw vips_jpegsave straight to the path (the pre-fix behaviour) → races.
 *   - OutputLocked…      ImageIO::encode (temp + rename) reports a typed OutputLockedError, immediately,
 *                        when the destination is held open (Windows-only behaviour; the lib never polls).
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment.
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/output_profile.hpp>

#include <vips/vips.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace Platemaker::Infrastructure {
namespace {

namespace fs = std::filesystem;

// Both helpers feed only the Windows-only SaveReportsOutputLockedWhenDestinationHeld branch below; on
// POSIX that test is GTEST_SKIP'd, leaving them unused — [[maybe_unused]] keeps -Werror=unused-function
// (GCC/Linux release) from failing the build.
[[maybe_unused]] Core::PixelBuffer blackImage(int w, int h)
{
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, w, h, nullptr), 0) << vips_error_buffer();
    return Core::PixelBuffer{img};
}

[[maybe_unused]] Models::OutputProfile jpegProfile()
{
    Models::OutputProfile p;
    p.outputFormat = Models::OutputFormat::JPEG;
    p.jpegOptions  = {90, Models::JpegSubsampling::YUV_444, true, false};
    return p;
}

/// Hammers `path` with vips_thumbnail — exactly what ThumbnailCache::generate() does for an output
/// tile — from another thread until told to stop.
class ThumbnailReader {
public:
    explicit ThumbnailReader(std::string path, int gapMicros = 0)
        : m_path(std::move(path)), m_gap(gapMicros) {}

    void start()
    {
        m_thread = std::thread([this] {
            while (!m_stop.load(std::memory_order_relaxed)) {
                VipsImage* t = nullptr;
                if (vips_thumbnail(m_path.c_str(), &t, 200,
                                   "no_rotate", TRUE, "size", VIPS_SIZE_DOWN, nullptr) == 0) {
                    g_object_unref(t);
                    ++m_reads;
                } else {
                    vips_error_clear();
                }
                if (m_gap) std::this_thread::sleep_for(std::chrono::microseconds(m_gap));
            }
        });
    }
    void stopJoin() { m_stop = true; if (m_thread.joinable()) m_thread.join(); }
    int reads() const { return m_reads.load(); }

private:
    std::string       m_path;
    int               m_gap;
    std::atomic<bool> m_stop{false};
    std::atomic<int>  m_reads{0};
    std::thread       m_thread;
};

// The pre-fix write path: an in-place vips_jpegsave straight to the destination. Under a concurrent
// reader this reproduces the exact failure the user hit ("unable to open for write, Invalid argument").
TEST(ImageIoConcurrencyTest, InPlaceWriteRacesAConcurrentThumbnailReader)
{
    const fs::path dir = fs::temp_directory_path() / "pm-ioconc-inplace";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string path = (dir / "output_001.jpg").string();

    { // seed the file so the reader always has something to open
        VipsImage* s = nullptr; vips_black(&s, 800, 1280, nullptr);
        vips_jpegsave(s, path.c_str(), nullptr); g_object_unref(s);
    }

    ThumbnailReader reader(path);
    reader.start();

    int failures = 0;
    std::string firstError;
    for (int i = 0; i < 400; ++i) {
        VipsImage* img = nullptr;
        vips_black(&img, 800, 1280, nullptr);
        if (vips_jpegsave(img, path.c_str(), "Q", 90, nullptr) != 0) {
            if (firstError.empty()) firstError = vips_error_buffer();
            vips_error_clear();
            ++failures;
        }
        g_object_unref(img);
    }
    reader.stopJoin();
    fs::remove_all(dir);

    std::cout << "[in-place vips_jpegsave] " << failures << "/400 writes FAILED while "
              << reader.reads() << " concurrent thumbnail reads ran"
              << (failures ? ("\n  first error: " + firstError) : std::string("")) << "\n";

    // Timing-dependent, so not asserted — but on Windows this reliably reports failures > 0, which is
    // the race the atomic ImageIO::encode (next test) removes.
    SUCCEED();
}

// The contract (G1 + G4): ImageIO::encode() encodes to a temp sibling and renames it over the
// destination. If another process holds the destination so the rename cannot complete, save() throws a
// typed OutputLockedError **immediately** — the lib does not poll — and leaves the existing file intact
// with no temp litter. The consumer owns any retry policy. (Holding a file to deny replacement is
// Windows-specific; POSIX rename replaces regardless of open handles, so the check is skipped there.)
TEST(ImageIoConcurrencyTest, SaveReportsOutputLockedWhenDestinationHeld)
{
#ifdef _WIN32
    const fs::path dir = fs::temp_directory_path() / "pm-ioconc-locked";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string path = (dir / "output_001.jpg").string();

    ImageIO    io;
    const auto prof = jpegProfile();
    io.encode(blackImage(800, 1280), path, prof); // seed
    const auto seededMtime = fs::last_write_time(path);

    // Hold the destination open with FILE_SHARE_READ only — denies write/delete, exactly like an open
    // viewer / Explorer preview / antivirus. This makes the atomic rename fail.
    const std::wstring wpath(path.begin(), path.end()); // temp path is ASCII
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    EXPECT_THROW(io.encode(blackImage(800, 1280), path, prof), OutputLockedError);

    // Original untouched (whole-or-nothing), and no temp left behind.
    EXPECT_EQ(fs::last_write_time(path), seededMtime);
    EXPECT_FALSE(fs::exists(path + ".pmtmp"));

    CloseHandle(h);
    fs::remove_all(dir);
#else
    GTEST_SKIP() << "destination-lock behaviour is Windows-specific (POSIX rename replaces regardless)";
#endif
}

} // namespace
} // namespace Platemaker::Infrastructure
