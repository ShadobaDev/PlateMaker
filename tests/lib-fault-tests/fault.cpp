/**
 * \file tests/lib-fault-tests/fault.cpp
 * \brief Which calls fail — see fault.hpp.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <dlfcn.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>

namespace Fault {

namespace {

struct Counter {
    int        calls  = 0;
    int        armed  = 0;         // the call that is shot; 0: none
    VipsImage* image  = nullptr;   // what it returns; null: it fails
};

// libvips calls back from its worker threads, so the counters are shared. Neither is ever destroyed: a
// wrapped libc function (fread) is still called while the program exits, by libgcov writing its counts,
// after a function-local static would have been.
std::mutex& lock()
{
    static auto* m = new std::mutex;
    return *m;
}

std::map<std::string, Counter>& counters()
{
    static auto* c = new std::map<std::string, Counter>;
    return *c;
}

Counter& wrapped(const char* function)
{
    const auto it = counters().find(function);
    if (it == counters().end())
        throw std::logic_error(std::string("no wrapper for ") + function + " (add it to wrappers.cpp)");
    return it->second;
}

void arm(const char* function, VipsImage* image, int n)
{
    const std::lock_guard<std::mutex> guard(lock());
    Counter& c = wrapped(function);
    if (c.image)
        g_object_unref(c.image);
    if (image)
        g_object_ref(image);
    c.armed = c.calls + n;
    c.image = image;
}

} // namespace

void failCall(const char* function, int n) { arm(function, nullptr, n); }

void returnImage(const char* function, VipsImage* image, int n) { arm(function, image, n); }

int calls(const char* function)
{
    const std::lock_guard<std::mutex> guard(lock());
    return wrapped(function).calls;
}

std::string reset()
{
    const std::lock_guard<std::mutex> guard(lock());
    std::string unreached;
    for (auto& [function, c] : counters()) {
        if (c.armed > c.calls)
            unreached += (unreached.empty() ? "" : ", ") + function;
        if (c.image)
            g_object_unref(c.image);
        c = Counter{};
    }
    return unreached;
}

int liveObjects()
{
    int n = 0;
    vips_object_map([](void*, void* count, void*) -> void* {
        ++*static_cast<int*>(count);
        return nullptr;
    }, &n, nullptr);
    return n;
}

bool declare(const char* function)
{
    const std::lock_guard<std::mutex> guard(lock());
    counters().emplace(function, Counter{});
    return true;
}

Shot shoot(const char* function, VipsImage** out)
{
    const std::lock_guard<std::mutex> guard(lock());
    Counter& c = counters()[function];
    if (++c.calls != c.armed)
        return Shot::Pass;
    if (!c.image)
        return Shot::Fail;
    if (!out)
        throw std::logic_error(std::string(function) + " has no image to return");
    *out    = c.image;   // the test's reference passes to the caller
    c.image = nullptr;
    return Shot::Image;
}

void* real(const char* function)
{
    void* f = dlsym(RTLD_NEXT, function);
    if (!f)
        throw std::logic_error(std::string("no real ") + function + ": " + dlerror());
    return f;
}

int vipsFailure(const char* function)
{
    vips_error("fault", "injected failure in %s", function);
    return -1;
}

// --- For the tests ---------------------------------------------------------------------------------

namespace {

class VipsEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        ASSERT_EQ(VIPS_INIT("platemaker-lib-fault-tests"), 0) << vips_error_buffer();
        vips_cache_set_max(0);   // a cached operation would hold references liveObjects() counts
    }
    void TearDown() override { vips_shutdown(); }
};

const auto* const g_vipsEnvironment = ::testing::AddGlobalTestEnvironment(new VipsEnvironment);

} // namespace

VipsImage* blackImage(int width, int height, int bands, VipsInterpretation type)
{
    VipsImage* black = nullptr;
    VipsImage* typed = nullptr;
    EXPECT_EQ(vips_black(&black, width, height, "bands", bands, nullptr), 0) << vips_error_buffer();
    EXPECT_EQ(vips_copy(black, &typed, "interpretation", type, nullptr), 0) << vips_error_buffer();
    g_object_unref(black);
    return typed;
}

std::string save(VipsImage* image, const std::string& path)
{
    EXPECT_EQ(vips_image_write_to_file(image, path.c_str(), nullptr), 0) << vips_error_buffer();
    return path;
}

Scratch::Scratch()
{
    const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / (std::string("pm_fault_") + test->name());
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    m_dir = dir.string();
}

Scratch::~Scratch()
{
    std::error_code ec;
    std::filesystem::remove_all(m_dir, ec);
}

std::string Scratch::file(const std::string& name) const { return (std::filesystem::path(m_dir) / name).string(); }

} // namespace Fault
