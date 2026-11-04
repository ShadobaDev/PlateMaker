/**
 * \file tests/lib-fault-tests/fault.hpp
 * \brief Fault injection: make one call the library makes into libvips, libarchive or libc fail.
 *
 * The test executable defines the functions itself (wrappers.cpp). On ELF, libplatemaker.so binds its
 * calls to the first definition in the process, which is the executable's, so every call the library
 * makes passes through a wrapper. A wrapper passes the call on to the real function unless a test armed
 * it, in which case it fails the way the real function fails. Only the library's (and the tests' own)
 * calls are seen: libvips and libarchive bind their internal calls to themselves.
 *
 * \code
 * Fault::failCall("vips_extract_area");      // the next call fails
 * Fault::failCall("vips_copy", 2);           // the second call from now fails
 * Fault::returnImage("vips_svgload_buffer", rgb);   // the next call succeeds with this image instead
 * \endcode
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef PLATEMAKER_TESTS_FAULT_HPP
#define PLATEMAKER_TESTS_FAULT_HPP

#include <gtest/gtest.h>

#include <vips/vips.h>

#include <string>

namespace Fault {

/// Makes the \p n-th call to \p function from now fail, once. Throws for a function without a wrapper.
void failCall(const char* function, int n = 1);

/**
 * \brief Makes the \p n-th call to \p function from now succeed with \p image instead of calling libvips,
 *        once — libvips behaving otherwise than it promises, for the guards against that.
 *
 * The caller of \p function receives a reference of its own; \p image stays the test's.
 */
void returnImage(const char* function, VipsImage* image, int n = 1);

/// Calls to \p function since the test began. Throws for a function without a wrapper.
[[nodiscard]] int calls(const char* function);

/// Forgets every count and disarms every fault. Returns the functions armed but never reached.
std::string reset();

/// libvips objects alive now: equal before and after a failure when nothing was leaked on its way out.
[[nodiscard]] int liveObjects();

// --- For the wrappers -----------------------------------------------------------------------------

/// Registers \p function as wrapped; called once per wrapper at start-up.
bool declare(const char* function);

enum class Shot { Pass, Fail, Image };

/// Counts a call to \p function and says what it does. Image only where \p out is given: it then holds
/// the test's image (throws for a function that has no image to return).
[[nodiscard]] Shot shoot(const char* function, VipsImage** out = nullptr);

/// The real \p function, the next definition after the executable's.
[[nodiscard]] void* real(const char* function);

/// Records a libvips failure, as a real one would, and returns libvips' error result.
int vipsFailure(const char* function);

// --- For the tests ---------------------------------------------------------------------------------

/// A black image of \p bands bands, tagged \p type; the caller owns it.
[[nodiscard]] VipsImage* blackImage(int width, int height, int bands,
                                    VipsInterpretation type = VIPS_INTERPRETATION_sRGB);

/// \p image saved to \p path (any format libvips writes by extension); the path, for convenience.
std::string save(VipsImage* image, const std::string& path);

/// A scratch folder for one test, removed with it.
class Scratch {
public:
    Scratch();
    ~Scratch();
    Scratch(const Scratch&)            = delete;
    Scratch& operator=(const Scratch&) = delete;
    [[nodiscard]] std::string file(const std::string& name) const;

private:
    std::string m_dir;
};

/// The message of the exception \p f throws, or "" when it throws none.
template <typename F>
std::string thrown(F&& f)
{
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

} // namespace Fault

/// \p text, evaluated once, contains \p part.
#define EXPECT_CONTAINS(text, part)                                                                       \
    do {                                                                                                  \
        const std::string fault_text_ = (text);                                                           \
        EXPECT_NE(fault_text_.find(part), std::string::npos) << fault_text_;                              \
    } while (0)

/// Every fault test derives from this: a clean slate before, and after, a failure if an armed fault never
/// fired (the test then proved nothing).
class FaultTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        Fault::reset();
        vips_error_clear();   // what an earlier test's failure left there
    }
    void TearDown() override
    {
        const std::string unreached = Fault::reset();
        EXPECT_TRUE(unreached.empty()) << "armed but never called: " << unreached;
    }
};

/**
 * \brief A libvips operation: `int vips_<op>(required..., ...)`. Passed on through vips_call_split(),
 *        options included. \p params is the declaration's parameter list with `...`, \p last its last
 *        named parameter, \p out its output image (`nullptr` when it makes none), and the rest the
 *        required arguments in order.
 */
#define FAULT_VIPS_OPERATION(op, params, last, out, ...)                                                  \
    static const bool fault_declared_##op = Fault::declare("vips_" #op);                                  \
    extern "C" int vips_##op params                                                                       \
    {                                                                                                     \
        switch (Fault::shoot("vips_" #op, out)) {                                                         \
        case Fault::Shot::Fail:  return Fault::vipsFailure("vips_" #op);                                  \
        case Fault::Shot::Image: return 0;                                                                \
        case Fault::Shot::Pass:  break;                                                                   \
        }                                                                                                 \
        va_list ap;                                                                                       \
        va_start(ap, last);                                                                               \
        const int r = vips_call_split(#op, ap, __VA_ARGS__);                                              \
        va_end(ap);                                                                                       \
        return r;                                                                                         \
    }

/**
 * \brief A libvips convenience function that is not an operation: `int vips_<name>(VipsImage* in,
 *        VipsImage** out, ...)`. Options cannot be passed on through `...`, so the real function gets
 *        none — the library passes none to these.
 */
#define FAULT_VIPS_IN_OUT(name)                                                                           \
    static const bool fault_declared_##name = Fault::declare("vips_" #name);                              \
    extern "C" int vips_##name(VipsImage* in, VipsImage** out, ...)                                       \
    {                                                                                                     \
        switch (Fault::shoot("vips_" #name, out)) {                                                       \
        case Fault::Shot::Fail:  return Fault::vipsFailure("vips_" #name);                                \
        case Fault::Shot::Image: return 0;                                                                \
        case Fault::Shot::Pass:  break;                                                                   \
        }                                                                                                 \
        using Real = int (*)(VipsImage*, VipsImage**, ...);                                               \
        return reinterpret_cast<Real>(Fault::real("vips_" #name))(in, out, nullptr);                     \
    }

/**
 * \brief A C function with a fixed parameter list (libvips, libarchive, libc). \p failure is what the
 *        real one returns on a failure; it may record the error first: `(errno = EIO, -1)`.
 */
#define FAULT_C_FUNCTION(ret, name, params, args, failure)                                                \
    static const bool fault_declared_##name = Fault::declare(#name);                                      \
    extern "C" ret name params                                                                            \
    {                                                                                                     \
        if (Fault::shoot(#name) == Fault::Shot::Fail)                                                     \
            return failure;                                                                               \
        using Real = ret(*) params;                                                                       \
        static const Real realFunction = reinterpret_cast<Real>(Fault::real(#name));                      \
        return realFunction args;                                                                         \
    }

#endif // PLATEMAKER_TESTS_FAULT_HPP
