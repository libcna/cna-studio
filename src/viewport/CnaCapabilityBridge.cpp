// SPDX-License-Identifier: MS-PL
/**
 * @file CnaCapabilityBridge.cpp
 * @brief Copying a live `RendererCapabilityProfile` into Studio's CNA-free snapshot.
 */

#include "CNA/Studio/Viewport/CnaCapabilityBridge.hpp"

#include <CNA/Platform/CurrentPlatform.hpp>
#include <CNA/Platform/IPlatform.hpp>
#include <CNA/RendererCapabilityProfile.hpp>
#include <Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp>

#ifdef CNA_CNAEXT
#include <CNA/Graphics/EngineLayerVersion.hpp>
#endif

#include <exception>
#include <string>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Translates CNA's four-state answer into Studio's. */
        StudioCapabilityAnswer translate(::CNA::RendererFeatureSupport support)
        {
            switch (support)
            {
                case ::CNA::RendererFeatureSupport::Supported:
                    return StudioCapabilityAnswer::Supported;
                case ::CNA::RendererFeatureSupport::Restricted:
                    return StudioCapabilityAnswer::Restricted;
                case ::CNA::RendererFeatureSupport::Unsupported:
                    return StudioCapabilityAnswer::Unsupported;
                case ::CNA::RendererFeatureSupport::Unknown:
                    break;
            }
            return StudioCapabilityAnswer::Unknown;
        }
    } // namespace

    StudioModernApiState captureStudioModernApiState()
    {
        StudioModernApiState state;
#ifdef CNA_CNAEXT
        // Present, and this is CNA's own statement about itself: the definition arrives through
        // CNA's cna_build_config interface target rather than being set anywhere in Studio.
        state.available = true;
        state.engineLayerVersion = ::CNA::Graphics::getEngineLayerVersion();
        state.detail = ::CNA::Graphics::getEngineLayerVersionString();

        // The macro is what this translation unit's header said; the function is what the library
        // it linked to says. CNA publishes both precisely so that a disagreement is visible, and a
        // disagreement means one of the two was rebuilt and the other was not -- which surfaces
        // later as a call that resolves to the wrong shape.
        if (state.engineLayerVersion != CNA_CNAEXT_ENGINE_VERSION)
        {
            state.versionMismatch = true;
            state.detail += " (header says " + std::to_string(CNA_CNAEXT_ENGINE_VERSION)
                          + "; rebuild both)";
        }
#else
        // Said as a build fact with the flag that produces it, because the fix is one CMake option
        // and a reader who is told only "unavailable" goes looking for a driver.
        state.detail = "this Studio was built against a CNA without the CNAEXT engine layer "
                       "(-DCNA_CNAEXT=OFF)";
#endif
        return state;
    }

    std::string getHostPlatformName()
    {
        // Guarded, and the guard is not defensive noise: GetCurrentPlatform() lazily *creates* the
        // build's default platform, which throws when none can be created. A diagnostic function
        // that throws while assembling a diagnostic is the worst possible failure mode for it.
        try
        {
            if (!::CNA::Platform::HasCurrentPlatform()) { return "unknown"; }
            return ::CNA::Platform::GetCurrentPlatform().GetName();
        }
        catch (const std::exception&)
        {
            return "unknown";
        }
    }

    StudioHostAssessment assessStudioHost(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool allowCompatibilityFallback)
    {
        StudioHostAssessment assessment;
        assessment.modernApi = captureStudioModernApiState();

        // One snapshot, two evaluations. Asking the device twice would be two chances for it to
        // answer differently, and a report whose two halves disagreed about the same renderer is
        // worse than either half alone.
        const StudioCapabilitySnapshot snapshot = captureStudioCapabilitySnapshot(
            device, std::move(platformName), assessment.modernApi.available);

        assessment.modern = evaluateStudioHost(snapshot, StudioHostProfile::Modern);
        assessment.compatibility = evaluateStudioHost(snapshot, StudioHostProfile::Compatibility);
        assessment.decision = resolveStudioUiBackend(assessment.modern, assessment.compatibility,
                                                     allowCompatibilityFallback);
        return assessment;
    }

    StudioCapabilitySnapshot captureStudioCapabilitySnapshot(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        std::string platformName, bool modernApiAvailable)
    {
        const ::CNA::RendererCapabilityProfile& profile = device.GetRendererCapabilityProfileEXT();

        StudioCapabilitySnapshot snapshot;
        snapshot.setRendererName(std::string{profile.GetRendererName()});
        snapshot.setPlatformName(std::move(platformName));
        snapshot.setModernApiAvailable(modernApiAvailable);

        // Every declared feature, walked through CNA's own span rather than a list written here. A
        // list would have to be updated when CNA adds a feature, and nothing would fail when it was
        // not -- the new feature would simply never appear in a diagnostic.
        for (const ::CNA::RendererFeature feature : ::CNA::AllRendererFeatures())
        {
            const ::CNA::RendererFeatureInfo& info = profile.GetFeature(feature);
            snapshot.setFeature(std::string{::CNA::GetRendererFeatureName(feature)},
                                translate(info.support), info.note);
        }

        for (const ::CNA::RendererLimit limit : ::CNA::AllRendererLimits())
        {
            const ::CNA::RendererLimitValue value = profile.GetLimit(limit);
            // An unknown limit is recorded as absent rather than as zero. Zero is a value, and a
            // requirement comparing against it would read "reports 0" for a renderer that in fact
            // reported nothing at all.
            if (value.known)
            {
                snapshot.setLimit(std::string{::CNA::GetRendererLimitName(limit)}, value.value);
            }
        }
        return snapshot;
    }
} // namespace CNA::Studio
