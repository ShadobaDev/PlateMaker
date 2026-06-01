/**
 * \file
 * \brief Slicer — encapsulates the slicing algorithm configuration and drives ScaledStrip.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#pragma once

#ifndef PLATEMAKER_CORE_SLICER_HPP
#define PLATEMAKER_CORE_SLICER_HPP

#include <vector>

#include <platemaker/cancellation_token.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Core {

/**
 * \class Slicer
 * \brief Configures and executes the slicing step of the processing pipeline.
 *
 * Slicer is constructed with the desired slice height and tail policy, then
 * applied to a populated ScaledStrip.  Separating configuration from execution
 * allows the same Slicer instance to be reused across multiple runs or to be
 * constructed from an OutputProfile and passed down to the pipeline.
 *
 * Internally, Slicer calls \c ScaledStrip::sliceAll() with its stored parameters.
 * It is the appropriate entry point for the pipeline orchestrator rather than
 * calling ScaledStrip directly.
 */
class Slicer {
public:
    /**
     * \brief Constructs a Slicer with the specified output parameters.
     *
     * \param sliceHeight Height of each full output slice in pixels.  Must be > 0.
     * \param policy      Policy applied to the final (potentially shorter) tail slice.
     */
    Slicer(int sliceHeight, Models::LastSlicePolicy policy) noexcept;

    /**
     * \brief Returns the configured slice height.
     */
    [[nodiscard]] int sliceHeight() const noexcept;

    /**
     * \brief Returns the configured last-slice policy.
     */
    [[nodiscard]] Models::LastSlicePolicy policy() const noexcept;

    /**
     * \brief Slices the given strip and returns all output slices.
     *
     * \param strip A populated ScaledStrip.
     * \return All SliceResult objects in order.
     *
     * \throws std::runtime_error if the strip is empty or configuration is invalid.
     */
    [[nodiscard]] std::vector<SliceResult> slice(const ScaledStrip& strip) const;

    /**
     * \brief Slices the given strip with cancellation support.
     *
     * \param strip       A populated ScaledStrip.
     * \param cancelToken Token polled between slices.
     * \return Completed SliceResult objects up to the point of cancellation.
     */
    [[nodiscard]] std::vector<SliceResult> slice(
        const ScaledStrip&       strip,
        const CancellationToken& cancelToken) const;

private:
    int                     m_sliceHeight; //!< Target height for each full output slice.
    Models::LastSlicePolicy m_policy;      //!< Tail-slice handling policy.
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_SLICER_HPP
