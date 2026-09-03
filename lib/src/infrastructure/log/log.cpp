/**
 * \file lib/src/infrastructure/log/log.cpp
 * \brief Component-gated diagnostic logger implementation.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-17
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/log/log.hpp>

#include <atomic>
#include <iostream>
#include <mutex>
#include <utility>

namespace Platemaker::Infrastructure::Log {

namespace {

// Enabled-component mask. Relaxed ordering is enough: this is an advisory diagnostic gate,
// not a synchronisation point. Default 0 → the whole library is silent until a host opts in.
std::atomic<std::uint64_t> g_enabled{0};

// The sink is a std::function that can be swapped at runtime; a mutex guards the swap against
// a concurrent log() from a worker thread. The lock is only ever taken on the enabled path.
std::mutex& sinkMutex() noexcept
{
    static std::mutex m;
    return m;
}

// Default sink: "[Component] message\n" to stderr, one line each.
void defaultEmit(std::uint64_t component, std::string_view message)
{
    std::clog << '[' << componentName(component) << "] " << message << '\n';
}

Sink& sinkRef()
{
    static Sink s = &defaultEmit;
    return s;
}

} // namespace

void setEnabledComponents(std::uint64_t mask) noexcept
{
    g_enabled.store(mask, std::memory_order_relaxed);
}

std::uint64_t enabledComponents() noexcept
{
    return g_enabled.load(std::memory_order_relaxed);
}

void enable(std::uint64_t components) noexcept
{
    g_enabled.fetch_or(components, std::memory_order_relaxed);
}

void disable(std::uint64_t components) noexcept
{
    g_enabled.fetch_and(~components, std::memory_order_relaxed);
}

bool isEnabled(std::uint64_t component) noexcept
{
    return (g_enabled.load(std::memory_order_relaxed) & component) != 0;
}

void setSink(Sink sink)
{
    std::lock_guard<std::mutex> lock(sinkMutex());
    sinkRef() = sink ? std::move(sink) : Sink{&defaultEmit};
}

void write(std::uint64_t component, std::string_view message)
{
    if (!isEnabled(component)) return;

    // Copy the sink under the lock, then call it unlocked so a sink that logs (or is swapped)
    // cannot deadlock. The copy cost is paid only on the enabled path.
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(sinkMutex());
        sink = sinkRef();
    }
    if (sink) sink(component, message);
}

const char* componentName(std::uint64_t component) noexcept
{
    switch (component) {
        case ProcessingPipeline:        return "ProcessingPipeline";
        case Scaler:                    return "Scaler";
        case ScaledStrip:               return "ScaledStrip";
        case MarginCropper:             return "MarginCropper";
        case CanvasProfileMatcher:      return "CanvasProfileMatcher";
        case ImageIO:                   return "ImageIO";
        case TemplateGenerator:         return "TemplateGenerator";
        case WorkspaceSerializer:       return "WorkspaceSerializer";
        case WorkspaceEditor:           return "WorkspaceEditor";
        case ProjectEditor:             return "ProjectEditor";
        case ThumbnailCache:            return "ThumbnailCache";
        case FileMetaData:              return "FileMetaData";
        case ColourCorrector:           return "ColourCorrector";
        case StripOverlayCompositor:    return "StripOverlayCompositor";
        case Memory:                    return "Memory";
        default:                        return "component";
    }
}

} // namespace Platemaker::Infrastructure::Log
