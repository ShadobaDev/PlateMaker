/**
 * \file
 * \brief CancellationToken implementation.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/cancellation_token.hpp>

namespace Platemaker {

void CancellationToken::cancel() noexcept
{
    m_cancelled.store(true, std::memory_order_release);
}

bool CancellationToken::isCancelled() const noexcept
{
    return m_cancelled.load(std::memory_order_acquire);
}

void CancellationToken::reset() noexcept
{
    m_cancelled.store(false, std::memory_order_relaxed);
}

} // namespace Platemaker
