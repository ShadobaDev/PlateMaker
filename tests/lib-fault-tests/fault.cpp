/**
 * \file tests/lib-fault-tests/fault.cpp
 * \brief Which calls fail — see fault.hpp.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <vips/vips.h>

#include <dlfcn.h>

#include <map>
#include <mutex>
#include <stdexcept>

namespace Fault {

namespace {

struct Counter {
    int calls  = 0;
    int failAt = 0;   // the call that fails; 0: none
};

// libvips calls back from its worker threads, so the counters are shared.
std::mutex& lock()
{
    static std::mutex m;
    return m;
}

std::map<std::string, Counter>& counters()
{
    static std::map<std::string, Counter> c;
    return c;
}

Counter& wrapped(const char* function)
{
    const auto it = counters().find(function);
    if (it == counters().end())
        throw std::logic_error(std::string("no wrapper for ") + function + " (add it to wrappers.cpp)");
    return it->second;
}

} // namespace

void failCall(const char* function, int n)
{
    const std::lock_guard<std::mutex> guard(lock());
    Counter& c = wrapped(function);
    c.failAt   = c.calls + n;
}

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
        if (c.failAt > c.calls)
            unreached += (unreached.empty() ? "" : ", ") + function;
        c = Counter{};
    }
    return unreached;
}

bool declare(const char* function)
{
    const std::lock_guard<std::mutex> guard(lock());
    counters().emplace(function, Counter{});
    return true;
}

bool hit(const char* function)
{
    const std::lock_guard<std::mutex> guard(lock());
    Counter& c = counters()[function];
    return ++c.calls == c.failAt;
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

} // namespace Fault
