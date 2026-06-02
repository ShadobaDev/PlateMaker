/**
 * \file
 * \brief Slicer implementation — thin wrapper that configures and drives ScaledStrip::sliceAll().
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/slicer/slicer.hpp>

namespace Platemaker::Core {

Slicer::Slicer(int sliceHeight, Models::LastSlicePolicy policy) noexcept
    : m_sliceHeight(sliceHeight)
    , m_policy(policy)
{}

int Slicer::sliceHeight() const noexcept { return m_sliceHeight; }

Models::LastSlicePolicy Slicer::policy() const noexcept { return m_policy; }

std::vector<SliceResult> Slicer::slice(const ScaledStrip& strip) const
{
    return strip.sliceAll(m_sliceHeight, m_policy);
}

std::vector<SliceResult> Slicer::slice(
    const ScaledStrip&                       strip,
    const Infrastructure::CancellationToken& cancelToken) const
{
    return strip.sliceAll(m_sliceHeight, m_policy, cancelToken);
}

} // namespace Platemaker::Core
