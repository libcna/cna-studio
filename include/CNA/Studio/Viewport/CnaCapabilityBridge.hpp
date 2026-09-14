// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Viewport/CnaCapabilityBridge.hpp
 * @brief Reads a live CNA device's capability profile into Studio's CNA-free snapshot.
 *
 * `plan.md` STUDIO-02021.
 *
 * This is the only place in Studio that touches `CNA::RendererCapabilityProfile`, and it is
 * deliberately the thinnest thing that could work: it copies every classified answer across,
 * converts nothing, and decides nothing. The contract and the whole of its evaluation live in
 * `StudioHostRequirements`, in a module with no CNA dependency at all — so every branch of the
 * decision, including ones only a renderer nobody owns could reach, is tested in CI with no GPU.
 *
 * Put the other way round: if this file made judgements, those judgements would only be testable
 * on hardware. It does not, so they are not.
 *
 * **Why the header names no CNA type.** Declaring a `const RendererCapabilityProfile&` parameter
 * would drag CNA's headers into every target that links `cna-studio-viewport`, which is exactly
 * what `STUDIO-02033`'s guard test exists to prevent. The device is forward-declared instead, the
 * profile is fetched inside the `.cpp`, and CNA stays a private link dependency.
 */

#include "CNA/Studio/Project/StudioHostRequirements.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Studio
{
    /**
     * @brief Captures what a live device reports, in CNA-free terms.
     *
     * Every one of CNA's declared features and limits is copied across, not only the ones Studio
     * currently asks about: the snapshot is also what the diagnostics panel and a bug report show,
     * and a snapshot filtered to today's requirement set would answer no question tomorrow's
     * requirement raises.
     *
     * @param device The device to interrogate.
     * @param platformName The CNA platform implementation's name, which the device does not carry.
     * @param modernApiAvailable Whether the modern CNAEXT graphics API is compiled in.
     * @return The snapshot.
     */
    [[nodiscard]] StudioCapabilitySnapshot captureStudioCapabilitySnapshot(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool modernApiAvailable);

    /**
     * @brief The name of the CNA platform implementation this build runs on.
     *
     * The *platform*, not the operating system: `"SDL3"` rather than `"Linux"`. Renderer and
     * platform are separate axes in current CNA (`docs/ARCHITECTURE.md` §2.1), and a diagnostic
     * that reported the OS in the platform's place would send every reader looking in the wrong
     * half of the configuration.
     *
     * @return The platform name, or `"unknown"` when no platform is installed.
     */
    [[nodiscard]] std::string getHostPlatformName();
} // namespace CNA::Studio
