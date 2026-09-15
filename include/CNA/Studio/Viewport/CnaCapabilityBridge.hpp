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

#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Studio
{
    /**
     * @brief What this Studio build actually carries of CNA's modern graphics layer.
     *
     * `plan.md` STUDIO-02071. Both hosts used to pass a literal `true` for `modernApiAvailable` to
     * @ref captureStudioCapabilitySnapshot, so the field reported what the call site *asserted*
     * rather than what the build had — and a Studio configured against a CNA with
     * `-DCNA_CNAEXT=OFF` would have claimed the modern API and then failed to find a type for it.
     *
     * This is the one adapter that answers the question, and it answers it from the build:
     * `CNA_CNAEXT` reaches this translation unit through CNA's own `cna_build_config` interface
     * target, so the compile definition is CNA's statement about itself rather than Studio's guess
     * about CNA.
     */
    struct StudioModernApiState
    {
        /** @brief Whether the CNAEXT engine layer is compiled into this Studio. */
        bool available = false;
        /**
         * @brief The revision the linked CNA library reports, or 0 when the layer is absent.
         *
         * The *library's* answer, not the header's. CNA publishes both deliberately — a macro for
         * what the translation unit compiled against and a function for what it linked to — and
         * when they disagree something was rebuilt and something else was not.
         */
        int engineLayerVersion = 0;
        /** @brief One sentence for the report, the log and the Diagnostics panel. */
        std::string detail;
        /**
         * @brief Set when the header and the library report different revisions.
         *
         * Not a reason to call the API unavailable: a mixed build has the layer and has it at an
         * unknown revision, which is a different and more alarming thing than not having it.
         */
        bool versionMismatch = false;
    };

    /**
     * @brief Reads this build's modern-API state. Never asserts one.
     * @return What the build has.
     */
    [[nodiscard]] StudioModernApiState captureStudioModernApiState();

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
     * @param modernApiAvailable Whether the modern CNAEXT graphics API is compiled in. Callers
     *        take this from @ref captureStudioModernApiState; it is a parameter rather than a call
     *        inside so that a test can evaluate a device against either answer.
     * @return The snapshot.
     */
    [[nodiscard]] StudioCapabilitySnapshot captureStudioCapabilitySnapshot(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool modernApiAvailable);

    /**
     * @brief Everything a host needs to decide whether and how to draw.
     *
     * `plan.md` STUDIO-02072. One type rather than four locals in two hosts: the ImGui host and the
     * native shell host asked the same three questions in the same order, and the day they stopped
     * agreeing would have been a day one of them started on a different renderer than the other
     * with nothing saying so.
     */
    struct StudioHostAssessment
    {
        /** @brief What this build carries of the modern layer. */
        StudioModernApiState modernApi;
        /** @brief The verdict under the modern profile. */
        StudioHostEvaluation modern;
        /** @brief The verdict under the compatibility profile. */
        StudioHostEvaluation compatibility;
        /** @brief Which backend to draw with, and the sentence explaining it. */
        StudioUiBackendDecision decision;

        /**
         * @brief The verdict a host reports and refuses on.
         *
         * The chosen profile's, so that `--host-capabilities` and the refusal diagnostic describe
         * the contract actually in force. When nothing was chosen it is the modern one, because
         * that is the requirement that was not met.
         */
        [[nodiscard]] const StudioHostEvaluation& effective() const
        {
            return decision.choice == StudioUiBackendChoice::Compatibility ? compatibility : modern;
        }

        /** @brief Whether any backend can draw on this host. */
        [[nodiscard]] bool canHostStudio() const
        {
            return decision.choice != StudioUiBackendChoice::None;
        }
    };

    /**
     * @brief Asks a live device both profiles and resolves a backend.
     *
     * @param device The device to interrogate.
     * @param platformName The CNA platform implementation's name.
     * @param allowCompatibilityFallback Whether the classic backend may be used when the modern
     *        profile is unmet. `--ui-renderer=modern` passes false.
     * @return The assessment.
     */
    [[nodiscard]] StudioHostAssessment assessStudioHost(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool allowCompatibilityFallback = true);

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
