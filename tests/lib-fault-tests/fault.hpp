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
 * \endcode
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef PLATEMAKER_TESTS_FAULT_HPP
#define PLATEMAKER_TESTS_FAULT_HPP

#include <gtest/gtest.h>

#include <string>

namespace Fault {

/// Makes the \p n-th call to \p function from now fail, once. Throws for a function without a wrapper.
void failCall(const char* function, int n = 1);

/// Calls to \p function since the test began. Throws for a function without a wrapper.
[[nodiscard]] int calls(const char* function);

/// Forgets every count and disarms every fault. Returns the functions armed but never reached.
std::string reset();

// --- For the wrappers -----------------------------------------------------------------------------

/// Registers \p function as wrapped; called once per wrapper at start-up.
bool declare(const char* function);

/// Counts a call to \p function; true when this call is the one to fail.
[[nodiscard]] bool hit(const char* function);

/// The real \p function, the next definition after the executable's.
[[nodiscard]] void* real(const char* function);

/// Records a libvips failure, as a real one would, and returns libvips' error result.
int vipsFailure(const char* function);

} // namespace Fault

/// Every fault test derives from this: a clean slate before, and after, a failure if an armed fault never
/// fired (the test then proved nothing).
class FaultTest : public ::testing::Test {
protected:
    void SetUp() override { Fault::reset(); }
    void TearDown() override
    {
        const std::string unreached = Fault::reset();
        EXPECT_TRUE(unreached.empty()) << "armed but never called: " << unreached;
    }
};

/**
 * \brief A libvips operation: `int vips_<op>(required..., ...)`. Passed on through vips_call_split(),
 *        options included. \p params is the declaration's parameter list with `...`, \p last its last
 *        named parameter, and the rest the required arguments in order.
 */
#define FAULT_VIPS_OPERATION(op, params, last, ...)                                                       \
    static const bool fault_declared_##op = Fault::declare("vips_" #op);                                  \
    extern "C" int vips_##op params                                                                       \
    {                                                                                                     \
        if (Fault::hit("vips_" #op))                                                                      \
            return Fault::vipsFailure("vips_" #op);                                                       \
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
        if (Fault::hit("vips_" #name))                                                                    \
            return Fault::vipsFailure("vips_" #name);                                                     \
        using Real = int (*)(VipsImage*, VipsImage**, ...);                                               \
        return reinterpret_cast<Real>(Fault::real("vips_" #name))(in, out, nullptr);                     \
    }

/**
 * \brief A C function with a fixed parameter list (libarchive, libc). \p failure is what the real one
 *        returns on a failure; it may set errno first: `(errno = EIO, -1)`.
 */
#define FAULT_C_FUNCTION(ret, name, params, args, failure)                                                \
    static const bool fault_declared_##name = Fault::declare(#name);                                      \
    extern "C" ret name params                                                                            \
    {                                                                                                     \
        if (Fault::hit(#name))                                                                            \
            return failure;                                                                               \
        using Real = ret(*) params;                                                                       \
        static const Real realFunction = reinterpret_cast<Real>(Fault::real(#name));                      \
        return realFunction args;                                                                         \
    }

#endif // PLATEMAKER_TESTS_FAULT_HPP
