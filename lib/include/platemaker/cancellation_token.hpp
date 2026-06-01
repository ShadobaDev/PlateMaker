/**
 * \file
 * \brief CancellationToken — a thread-safe flag for cooperative pipeline cancellation.
 *
 * The token is created by the caller (GUI thread / CLI main) and passed into
 * pipeline calls by const-reference.  The pipeline checks it between slices and
 * exits early when it is set.  The token does not throw — it is purely advisory.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CANCELLATION_TOKEN_HPP
#define PLATEMAKER_CANCELLATION_TOKEN_HPP

#include <atomic>

/**
 * \namespace Platemaker
 * \brief Top-level namespace for the entire Platemaker library and applications.
 */
namespace Platemaker {

/**
 * \class CancellationToken
 * \brief A lightweight, thread-safe cancellation flag for cooperative cancellation.
 *
 * One token is created per processing run.  The owner (e.g. a GUI worker thread
 * or the CLI signal handler) calls \c cancel() to request early termination.  The
 * pipeline checks \c isCancelled() between each output slice and returns early if
 * the flag is set.
 *
 * \note Already-written output files are kept when the pipeline is cancelled —
 *       it is the caller's responsibility to decide whether to delete them.
 *
 * \warning Calling \c cancel() is non-blocking and does not wait for the pipeline
 *          to finish.  The caller must join the worker thread separately.
 */
class CancellationToken {
public:
    CancellationToken() noexcept = default;

    // Non-copyable — a token must have a single authoritative owner.
    CancellationToken(const CancellationToken&)            = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    // Movable — allows transferring ownership when constructing worker objects.
    CancellationToken(CancellationToken&&) noexcept            = delete;
    CancellationToken& operator=(CancellationToken&&) noexcept = delete;

    /**
     * \brief Request cancellation.
     *
     * Sets the internal flag to \c true.  Thread-safe; may be called from any thread.
     */
    void cancel() noexcept;

    /**
     * \brief Returns \c true if cancellation has been requested.
     *
     * Thread-safe; may be polled from any thread.
     *
     * \return \c true if \c cancel() has been called; \c false otherwise.
     */
    [[nodiscard]] bool isCancelled() const noexcept;

    /**
     * \brief Resets the token so the same instance can be reused for a new run.
     *
     * Must only be called from the owning thread before starting a new pipeline run.
     */
    void reset() noexcept;

private:
    std::atomic<bool> m_cancelled{false}; //!< The underlying atomic flag.
};

} // namespace Platemaker

#endif // PLATEMAKER_CANCELLATION_TOKEN_HPP
